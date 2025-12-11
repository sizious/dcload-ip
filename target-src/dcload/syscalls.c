#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <utime.h>
#include <stdarg.h>
#include <dirent.h>
#include <string.h>
#include "syscalls.h"
#include "packet.h"
#include "net.h"
#include "commands.h"
#include "scif.h"
#include "adapter.h"

unsigned short dcload_syscall_port = 31313;
unsigned int syscall_retval = 0;
unsigned char* syscall_data;

static struct dirent our_dir;

static volatile unsigned int console_seq_tx = 0;
static volatile unsigned int console_seq_ack = 0;
static volatile unsigned int console_credits = CONSOLE_CREDIT_INIT;
static volatile unsigned int console_inflight = 0;
static volatile unsigned int console_last_tx_time = 0;
static volatile unsigned int console_adaptive_credits = CONSOLE_CREDIT_INIT;

__attribute__((aligned(32))) static unsigned char console_ring[CONSOLE_RING_SIZE];
static volatile unsigned int console_ring_wr = 0;
static volatile unsigned int console_ring_rd = 0;
static volatile unsigned int console_ring_sent = 0;

#define CONSOLE_RING_MASK (CONSOLE_RING_SIZE - 1)

static inline unsigned int tmr_us(void)
{
	volatile unsigned int *tmr = (volatile unsigned int *)0xffd80010;
	return *tmr;
}

static inline unsigned int console_ring_used(void)
{
	return (console_ring_wr - console_ring_rd) & CONSOLE_RING_MASK;
}

static inline unsigned int console_ring_pending(void)
{
	return (console_ring_wr - console_ring_sent) & CONSOLE_RING_MASK;
}

static inline unsigned int console_ring_free(void)
{
	return CONSOLE_RING_SIZE - 1 - console_ring_used();
}

static void console_ring_write(const unsigned char *data, unsigned int len)
{
	unsigned int wr = console_ring_wr;
	unsigned int end1 = CONSOLE_RING_SIZE - wr;
	
	if (end1 >= len)
	{
		for (unsigned int i = 0; i < len; i++)
			console_ring[wr + i] = data[i];
	}
	else
	{
		for (unsigned int i = 0; i < end1; i++)
			console_ring[wr + i] = data[i];
		for (unsigned int i = end1; i < len; i++)
			console_ring[i - end1] = data[i];
	}
	
	console_ring_wr = (wr + len) & CONSOLE_RING_MASK;
}

static unsigned int console_ring_peek(unsigned char *out, unsigned int max_len)
{
	unsigned int pending = console_ring_pending();
	if (pending > max_len) pending = max_len;
	unsigned int rd = console_ring_sent;
	unsigned int end1 = CONSOLE_RING_SIZE - rd;
	
	if (end1 >= pending)
	{
		for (unsigned int i = 0; i < pending; i++)
			out[i] = console_ring[rd + i];
	}
	else
	{
		for (unsigned int i = 0; i < end1; i++)
			out[i] = console_ring[rd + i];
		for (unsigned int i = end1; i < pending; i++)
			out[i] = console_ring[i - end1];
	}
	return pending;
}

static void console_ring_mark_sent(unsigned int len)
{
	console_ring_sent = (console_ring_sent + len) & CONSOLE_RING_MASK;
	console_inflight += len;
}

static void console_ring_ack(unsigned int acked_bytes)
{
	unsigned int advance = acked_bytes;
	unsigned int used = console_ring_used();
	if (advance > used) advance = used;
	console_ring_rd = (console_ring_rd + advance) & CONSOLE_RING_MASK;
	if (console_inflight >= acked_bytes)
		console_inflight -= acked_bytes;
	else
		console_inflight = 0;
}

size_t strlen(const char *s)
{
	int c = 0;

	while (s[c] != 0)
		c++;
	return c;
}

void build_send_packet(int command_len)
{
	ether_header_t * ether = (ether_header_t *)pkt_buf;
	ip_header_t * ip = (ip_header_t *)(pkt_buf + ETHER_H_LEN);
	udp_header_t * udp = (udp_header_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN);

	make_ether(tool_mac, bb->mac, ether);
	make_ip(tool_ip, our_ip, UDP_H_LEN + command_len, IP_UDP_PROTOCOL, ip, 0);
	make_udp(tool_port, dcload_syscall_port, command_len, ip, udp);
	bb->start();
	bb->tx(pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + command_len);
}

static void send_console_packet(unsigned int len)
{
	ether_header_t * ether = (ether_header_t *)pkt_buf;
	ip_header_t * ip = (ip_header_t *)(pkt_buf + ETHER_H_LEN);
	udp_header_t * udp = (udp_header_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN);
	command_console_t *cmd = (command_console_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);

	memcpy(cmd->id, CMD_CWRITE, 4);
	cmd->seq = htonl(console_seq_tx);
	cmd->ack_seq = 0;
	cmd->credits = htons((unsigned short)console_inflight);
	cmd->len = htons(len);

	unsigned int pkt_len = CONSOLE_CMD_LEN + len;

	make_ether(tool_mac, bb->mac, ether);
	make_ip(tool_ip, our_ip, UDP_H_LEN + pkt_len, IP_UDP_PROTOCOL, ip, (unsigned short)console_seq_tx);
	make_udp(tool_port, dcload_syscall_port, pkt_len, ip, udp);
	bb->tx(pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + pkt_len);
	
	console_last_tx_time = tmr_us();
}

void console_pump(void)
{
	unsigned int burst = 0;
	unsigned int pending = console_ring_pending();
	
	if (pending == 0)
		return;
	
	unsigned int effective_credits = console_adaptive_credits;
	if (console_credits > effective_credits)
		effective_credits = console_credits;
	
	unsigned int can_send = effective_credits;
	
	if (console_inflight > 0)
	{
		unsigned int inflight_pkts = (console_inflight + CONSOLE_MAX_INLINE - 1) / CONSOLE_MAX_INLINE;
		if (can_send > inflight_pkts)
			can_send -= inflight_pkts;
		else
			can_send = 1;
	}
	
	unsigned int free_space = console_ring_free();
	int pressure = (free_space < CONSOLE_MIN_HEADROOM);
	
	if (CONSOLE_FIRE_FORGET)
	{
		if (pressure || pending >= CONSOLE_MAX_INLINE)
			can_send = CONSOLE_BURST_MAX * 2;
		else if (free_space >= CONSOLE_NOBLOCK_THRESH)
			can_send = CONSOLE_BURST_MAX;
	}
	
	unsigned int max_burst = pressure ? CONSOLE_BURST_MAX * 2 : CONSOLE_BURST_MAX;
	
	while (pending > 0 && can_send > 0 && burst < max_burst)
	{
		command_console_t *cmd = (command_console_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);
		unsigned int chunk = pending;
		if (chunk > CONSOLE_MAX_INLINE) chunk = CONSOLE_MAX_INLINE;
		
		console_ring_peek(cmd->data, chunk);
		send_console_packet(chunk);
		console_ring_mark_sent(chunk);
		
		console_seq_tx++;
		can_send--;
		burst++;
		
		pending = console_ring_pending();
	}
}

void console_drain(void)
{
	unsigned int drain_attempts = 0;
	unsigned int pending = console_ring_pending();
	
	console_credits = CONSOLE_CREDIT_BOOST;
	console_adaptive_credits = CONSOLE_CREDIT_BOOST;
	
	while (pending > 0 && drain_attempts < CONSOLE_DRAIN_SPINS)
	{
		console_pump();
		pending = console_ring_pending();
		
		if (pending == 0)
			break;
		
		drain_attempts++;
		console_inflight = 0;
		console_credits = CONSOLE_CREDIT_BOOST;
	}
	
	console_ring_rd = console_ring_wr;
	console_ring_sent = console_ring_wr;
	console_inflight = 0;
}

int console_credits_available(void)
{
	if (console_ring_free() >= CONSOLE_NOBLOCK_THRESH)
		return 1;
	return console_credits > 0;
}

void console_handle_ack(unsigned int ack_seq, unsigned int credits)
{
	unsigned int seq_diff = 0;
	
	if (ack_seq > console_seq_ack)
		seq_diff = ack_seq - console_seq_ack;
	else if (console_seq_ack > 0xFFFF0000 && ack_seq < 0x10000)
		seq_diff = (0xFFFFFFFF - console_seq_ack) + ack_seq + 1;
	
	if (seq_diff > 0)
	{
		console_seq_ack = ack_seq;
		unsigned int acked_bytes = seq_diff * CONSOLE_MAX_INLINE;
		console_ring_ack(acked_bytes);
		
		if (console_adaptive_credits < CONSOLE_CREDIT_BOOST)
		{
			console_adaptive_credits += seq_diff * 2;
			if (console_adaptive_credits > CONSOLE_CREDIT_BOOST)
				console_adaptive_credits = CONSOLE_CREDIT_BOOST;
		}
	}
	
	if (credits > 0)
	{
		console_credits = credits;
		if (console_credits > CONSOLE_CREDIT_BOOST * 2)
			console_credits = CONSOLE_CREDIT_BOOST * 2;
	}
	else
	{
		console_credits = CONSOLE_CREDIT_INIT;
	}
	
	if (console_ring_pending() > 0)
		console_pump();
}

static int syscall_wait_response(int timeout_secs)
{
	timeout_loop = timeout_secs;
	bb->loop(0);

	if (timeout_loop < 0) {
		timeout_loop = 0;
		return -1;
	}

	timeout_loop = 0;
	return 0;
}

static int syscall_with_retry(int command_len, int timeout_secs, int max_retries)
{
	int retries = 0;

	while (retries < max_retries) {
		build_send_packet(command_len);
		if (syscall_wait_response(timeout_secs) == 0)
			return 0;
		retries++;
	}

	return -1;
}

void dcexit(void)
{
	console_drain();
	bb->stop();

	command_t * command = (command_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);

	memcpy(command->id, CMD_EXIT, 4);
	command->address = 0;
	command->size = 0;
	build_send_packet(COMMAND_LEN);
}

int read(int fd, void *buf, size_t count)
{
	console_pump();
	
	command_3int_t * command = (command_3int_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);

	memcpy(command->id, CMD_READ, 4);
	command->value0 = htonl(fd);
	command->value1 = htonl((unsigned int)buf);
	command->value2 = htonl(count);

	if (syscall_with_retry(sizeof(command_3int_t),
		SYSCALL_TIMEOUT_SECS,
		SYSCALL_MAX_RETRIES) < 0)
	{
		return -1;
	}

	return syscall_retval;
}

int write(int fd, const void *buf, size_t count)
{
	if ((fd == 1 || fd == 2) && count <= CONSOLE_MAX_INLINE)
	{
		if (DCTOOL_MAJOR >= 2)
		{
			const unsigned char *src = (const unsigned char *)buf;
			unsigned int free_space = console_ring_free();
			size_t written = 0;
			
			if (free_space < count + CONSOLE_MIN_HEADROOM)
			{
				console_pump();
				free_space = console_ring_free();
			}
			
			if (free_space < count && CONSOLE_DROP_OLDEST)
			{
				unsigned int drop = count - free_space + CONSOLE_MIN_HEADROOM;
				if (drop > CONSOLE_RING_SIZE / 8)
					drop = CONSOLE_RING_SIZE / 8;
				
				unsigned int used = console_ring_used();
				unsigned int unsent = console_ring_pending();
				
				if (used > unsent)
				{
					unsigned int old_data = used - unsent;
					if (drop > old_data)
						drop = old_data;
					console_ring_rd = (console_ring_rd + drop) & CONSOLE_RING_MASK;
				}
				free_space = console_ring_free();
			}
			
			if (free_space >= count)
			{
				console_ring_write(src, count);
				written = count;
			}
			else if (free_space > 0)
			{
				console_ring_write(src, free_space);
				written = free_space;
			}
			
			unsigned int pending = console_ring_pending();
			int flush_now = 0;
			
			for (unsigned int i = 0; i < count; i++)
			{
				if (src[i] == '\n') { flush_now = 1; break; }
			}
			
			if (flush_now || pending >= CONSOLE_MAX_INLINE)
				console_pump();
			else if (pending > CONSOLE_MIN_HEADROOM && console_credits > CONSOLE_CREDIT_LOW)
				console_pump();
			
			return written;
		}
		
		command_3int_t * command = (command_3int_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);
		unsigned char *payload = (unsigned char *)(command + 1);

		memcpy(command->id, CMD_WRITE_OLD, 4);
		command->value0 = htonl(fd);
		command->value1 = htonl((unsigned int)buf);
		command->value2 = htonl(count);

		const unsigned char *src = (const unsigned char *)buf;
		unsigned int i = 0;
		
		while (i + 8 <= count)
		{
			*(unsigned long long *)(payload + i) = *(const unsigned long long *)(src + i);
			i += 8;
		}
		while (i < count)
		{
			payload[i] = src[i];
			i++;
		}

		bb->start();
		ether_header_t * ether = (ether_header_t *)pkt_buf;
		ip_header_t * ip = (ip_header_t *)(pkt_buf + ETHER_H_LEN);
		udp_header_t * udp = (udp_header_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN);

		make_ether(tool_mac, bb->mac, ether);
		make_ip(tool_ip, our_ip, UDP_H_LEN + sizeof(command_3int_t) + count, IP_UDP_PROTOCOL, ip, 0);
		make_udp(tool_port, dcload_syscall_port, sizeof(command_3int_t) + count, ip, udp);
		bb->tx(pkt_buf, ETHER_H_LEN + IP_H_LEN + UDP_H_LEN + sizeof(command_3int_t) + count);

		return count;
	}

	console_pump();

	command_3int_t * command = (command_3int_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);

	if(DCTOOL_MAJOR < 2)
		memcpy(command->id, CMD_WRITE_OLD, 4);
	else
		memcpy(command->id, CMD_WRITE, 4);

	command->value0 = htonl(fd);
	command->value1 = htonl((unsigned int)buf);
	command->value2 = htonl(count);

	if (syscall_with_retry(sizeof(command_3int_t),
		SYSCALL_WRITE_TIMEOUT_SECS,
		SYSCALL_WRITE_MAX_RETRIES) < 0)
	{
		return -1;
	}

	return syscall_retval;
}

int open(const char *pathname, int flags, ...)
{
	va_list ap;
	command_2int_string_t * command = (command_2int_string_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);

	int namelen = strlen(pathname);

	memcpy(command->id, CMD_OPEN, 4);

	va_start(ap, flags);
	command->value0 = htonl(flags);
	command->value1 = htonl(va_arg(ap, int));
	va_end(ap);

	memcpy(command->string, pathname, namelen+1);

	if (syscall_with_retry(sizeof(command_2int_string_t)+namelen,
		SYSCALL_TIMEOUT_SECS,
		SYSCALL_MAX_RETRIES) < 0)
	{
		return -1;
	}

	return syscall_retval;
}

int close(int fd)
{
	command_int_t * command = (command_int_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);

	memcpy(command->id, CMD_CLOSE, 4);
	command->value0 = htonl(fd);

	if (syscall_with_retry(sizeof(command_int_t),
		SYSCALL_TIMEOUT_SECS,
		SYSCALL_MAX_RETRIES) < 0)
	{
		return -1;
	}

	return syscall_retval;
}

int creat(const char *pathname, mode_t mode)
{
	command_int_string_t * command = (command_int_string_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);

	int namelen = strlen(pathname);

	memcpy(command->id, CMD_CREAT, 4);

	command->value0 = htonl(mode);

	memcpy(command->string, pathname, namelen+1);

	if (syscall_with_retry(sizeof(command_int_string_t)+namelen,
		SYSCALL_TIMEOUT_SECS,
		SYSCALL_MAX_RETRIES) < 0)
	{
		return -1;
	}

	return syscall_retval;
}

int link(const char *oldpath, const char *newpath)
{
	command_string_t * command = (command_string_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);
	int namelen1 = strlen(oldpath);
	int namelen2 = strlen(newpath);

	memcpy(command->id, CMD_LINK, 4);

	memcpy(command->string, oldpath, namelen1 + 1);
	memcpy(command->string + namelen1 + 1, newpath, namelen2 + 1);

	if (syscall_with_retry(sizeof(command_string_t)+namelen1+namelen2+1,
		SYSCALL_TIMEOUT_SECS,
		SYSCALL_MAX_RETRIES) < 0)
	{
		return -1;
	}

	return syscall_retval;

}

int unlink(const char *pathname)
{
	command_string_t * command = (command_string_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);
	int namelen = strlen(pathname);

	memcpy(command->id, CMD_UNLINK, 4);

	memcpy(command->string, pathname, namelen + 1);

	if (syscall_with_retry(sizeof(command_string_t)+namelen,
		SYSCALL_TIMEOUT_SECS,
		SYSCALL_MAX_RETRIES) < 0)
	{
		return -1;
	}

	return syscall_retval;
}

int chdir(const char *path)
{
	command_string_t * command = (command_string_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);
	int namelen = strlen(path);

	memcpy(command->id, CMD_CHDIR, 4);

	memcpy(command->string, path, namelen + 1);

	if (syscall_with_retry(sizeof(command_string_t)+namelen,
		SYSCALL_TIMEOUT_SECS,
		SYSCALL_MAX_RETRIES) < 0)
	{
		return -1;
	}

	return syscall_retval;
}

int chmod(const char *path, mode_t mode)
{
	command_int_string_t * command = (command_int_string_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);

	int namelen = strlen(path);

	memcpy(command->id, CMD_CHMOD, 4);

	command->value0 = htonl(mode);

	memcpy(command->string, path, namelen+1);

	if (syscall_with_retry(sizeof(command_int_string_t)+namelen,
		SYSCALL_TIMEOUT_SECS,
		SYSCALL_MAX_RETRIES) < 0)
	{
		return -1;
	}

	return syscall_retval;
}

off_t lseek(int fildes, off_t offset, int whence)
{
	command_3int_t * command = (command_3int_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);

	memcpy(command->id, CMD_LSEEK, 4);
	command->value0 = htonl(fildes);
	command->value1 = htonl(offset);
	command->value2 = htonl(whence);

	if (syscall_with_retry(sizeof(command_3int_t),
		SYSCALL_TIMEOUT_SECS,
		SYSCALL_MAX_RETRIES) < 0)
	{
		return -1;
	}

	return syscall_retval;
}

int fstat(int filedes, struct stat *buf)
{
	command_3int_t * command = (command_3int_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);

	memcpy(command->id, CMD_FSTAT, 4);
	command->value0 = htonl(filedes);
	command->value1 = htonl((unsigned int)buf);
	command->value2 = htonl(sizeof(struct stat));

	if (syscall_with_retry(sizeof(command_3int_t),
		SYSCALL_TIMEOUT_SECS,
		SYSCALL_MAX_RETRIES) < 0)
	{
		return -1;
	}

	return syscall_retval;
}

time_t time(time_t * t)
{
	command_int_t * command = (command_int_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);

	memcpy(command->id, CMD_TIME, 4);
	command->value0 = htonl((unsigned int)t);

	if (syscall_with_retry(sizeof(command_int_t),
		SYSCALL_TIMEOUT_SECS,
		SYSCALL_MAX_RETRIES) < 0)
	{
		if(t != NULL) *t = 0;
		return 0;
	}

	if(t != NULL)
	{
		*t = syscall_retval;
	}

	return syscall_retval;
}

int stat(const char *file_name, struct stat *buf)
{
	command_2int_string_t * command = (command_2int_string_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);
	int namelen = strlen(file_name);

	memcpy(command->id, CMD_STAT, 4);
	memcpy(command->string, file_name, namelen+1);

	command->value0 = htonl((unsigned int)buf);
	command->value1 = htonl(sizeof(struct stat));

	if (syscall_with_retry(sizeof(command_2int_string_t)+namelen,
		SYSCALL_TIMEOUT_SECS,
		SYSCALL_MAX_RETRIES) < 0)
	{
		return -1;
	}

	return syscall_retval;
}

int utime(const char *filename, struct utimbuf *buf)
{
	command_3int_string_t * command = (command_3int_string_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);
	int namelen = strlen(filename);

	memcpy(command->id, CMD_UTIME, 4);
	memcpy(command->string, filename, namelen+1);

	command->value0 = htonl((unsigned int)buf);

	if (buf) {
		command->value1 = htonl(buf->actime);
		command->value2 = htonl(buf->modtime);
	}

	if (syscall_with_retry(sizeof(command_3int_string_t)+namelen,
		SYSCALL_TIMEOUT_SECS,
		SYSCALL_MAX_RETRIES) < 0)
	{
		return -1;
	}

	return syscall_retval;
}

DIR * opendir(const char *name)
{
	command_string_t * command = (command_string_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);
	int namelen = strlen(name);

	memcpy(command->id, CMD_OPENDIR, 4);
	memcpy(command->string, name, namelen+1);

	if (syscall_with_retry(sizeof(command_string_t)+namelen,
		SYSCALL_TIMEOUT_SECS,
		SYSCALL_MAX_RETRIES) < 0)
	{
		return (DIR *)0;
	}

	return (DIR *)syscall_retval;
}

int closedir(DIR *dir)
{
	command_int_t * command = (command_int_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);

	memcpy(command->id, CMD_CLOSEDIR, 4);
	command->value0 = htonl((unsigned int)dir);

	if (syscall_with_retry(sizeof(command_int_t),
		SYSCALL_TIMEOUT_SECS,
		SYSCALL_MAX_RETRIES) < 0)
	{
		return -1;
	}

	return syscall_retval;
}

struct dirent *readdir(DIR *dir)
{
	command_3int_t * command = (command_3int_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);

	memcpy(command->id, CMD_READDIR, 4);
	command->value0 = htonl((unsigned int)dir);
	command->value1 = htonl((unsigned int)&our_dir);
	command->value2 = htonl(sizeof(struct dirent));

	if (syscall_with_retry(sizeof(command_3int_t),
		SYSCALL_TIMEOUT_SECS,
		SYSCALL_MAX_RETRIES) < 0)
	{
		return 0;
	}

	if (syscall_retval)
		return &our_dir;
	else
		return 0;
}

int gethostinfo(unsigned int *ip, unsigned int *port)
{
	*ip = tool_ip;
	*port = tool_port;

	return our_ip;
}

size_t gdbpacket(const char *in_buf, unsigned int size_pack, char* out_buf)
{
	size_t in_size = size_pack >> 16, out_size = size_pack & 0xffff;
	command_2int_string_t * command = (command_2int_string_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);

	memcpy(command->id, CMD_GDBPACKET, 4);
	command->value0 = htonl(in_size);
	command->value1 = htonl(out_size);
	memcpy(command->string, in_buf, in_size);

	if (syscall_with_retry(sizeof(command_2int_string_t)-1 + in_size,
		SYSCALL_TIMEOUT_SECS,
		SYSCALL_MAX_RETRIES) < 0)
	{
		return 0;
	}

	if (syscall_retval <= out_size)
		memcpy(out_buf, syscall_data, syscall_retval);

	return syscall_retval;
}

int rewinddir(DIR *dir)
{
	command_int_t * command = (command_int_t *)(pkt_buf + ETHER_H_LEN + IP_H_LEN + UDP_H_LEN);

	memcpy(command->id, CMD_REWINDDIR, 4);
	command->value0 = htonl((unsigned int)dir);

	if (syscall_with_retry(sizeof(command_int_t),
		SYSCALL_TIMEOUT_SECS,
		SYSCALL_MAX_RETRIES) < 0)
	{
		return -1;
	}

	return syscall_retval;
}

int flush_write_buffer(void)
{
	return 0;
}

int write_buffered(int fd, const void *buf, unsigned int count)
{
	return write(fd, buf, count);
}
