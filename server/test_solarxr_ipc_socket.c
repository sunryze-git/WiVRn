// SPDX-License-Identifier: GPL-3.0-or-later
#include "solarxr_ipc_socket.h"

#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define check(condition)                                                                                  \
	do                                                                                                \
	{                                                                                                 \
		if (!(condition))                                                                         \
		{                                                                                         \
			fprintf(stderr, "%s:%d: %s (errno=%d)\n", __FILE__, __LINE__, #condition, errno); \
			exit(EXIT_FAILURE);                                                               \
		}                                                                                         \
	} while (0)

struct fixture
{
	struct solarxr_ipc_socket socket;
	struct sockaddr_un address;
	char directory[64];
	int listener, peer;
};

static void fixture_init(struct fixture * fixture)
{
	memset(fixture, 0, sizeof(*fixture));
	strcpy(fixture->directory, "/tmp/wivrn-solarxr-test-XXXXXX");
	check(mkdtemp(fixture->directory) != NULL);
	fixture->address.sun_family = AF_UNIX;
	check(snprintf(fixture->address.sun_path, sizeof(fixture->address.sun_path), "%s/socket", fixture->directory) > 0);
	fixture->listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	check(fixture->listener >= 0);
	check(bind(fixture->listener, (struct sockaddr *)&fixture->address, sizeof(fixture->address)) == 0);
	check(listen(fixture->listener, 4) == 0);
	solarxr_ipc_socket_init(&fixture->socket, U_LOGGING_RAW);
	check(solarxr_ipc_socket_connect(&fixture->socket, fixture->address));
	fixture->peer = accept(fixture->listener, NULL, NULL);
	check(fixture->peer >= 0);
}

static void fixture_destroy(struct fixture * fixture)
{
	solarxr_ipc_socket_destroy(&fixture->socket);
	check(close(fixture->peer) == 0);
	check(close(fixture->listener) == 0);
	check(unlink(fixture->address.sun_path) == 0);
	check(rmdir(fixture->directory) == 0);
}

static void send_bytes(int fd, const void * data, size_t length)
{
	const unsigned char * bytes = data;
	while (length > 0)
	{
		ssize_t count = send(fd, bytes, length, MSG_NOSIGNAL);
		if (count < 0 && errno == EINTR)
			continue;
		check(count > 0);
		bytes += count;
		length -= count;
	}
}

static void check_fresh_frame(struct fixture * fixture)
{
	static const char payload[] = "new connection payload";
	uint32_t length = htole32(sizeof(length) + sizeof(payload));
	send_bytes(fixture->peer, &length, sizeof(length));
	send_bytes(fixture->peer, payload, sizeof(payload));
	check(solarxr_ipc_socket_receive(&fixture->socket) == sizeof(payload));
	check(memcmp(fixture->socket.buffer, payload, sizeof(payload)) == 0);
}

static void test_partial_frame(bool partial_header, unsigned failed_attempts)
{
	struct fixture fixture;
	fixture_init(&fixture);
	uint32_t length = htole32(8192 + sizeof(length));
	if (partial_header)
		send_bytes(fixture.peer, &length, 2);
	else
	{
		char payload[6000] = {0};
		send_bytes(fixture.peer, &length, sizeof(length));
		send_bytes(fixture.peer, payload, sizeof(payload));
	}
	check(solarxr_ipc_socket_receive(&fixture.socket) == 0);
	check(close(fixture.peer) == 0);
	check(!solarxr_ipc_socket_wait_timeout(&fixture.socket, 0));

	struct sockaddr_un missing = {.sun_family = AF_UNIX};
	check(snprintf(missing.sun_path, sizeof(missing.sun_path), "%s/missing", fixture.directory) > 0);
	for (unsigned attempt = 0; attempt < failed_attempts; ++attempt)
	{
		check(!solarxr_ipc_socket_reconnect(&fixture.socket, missing));
		check(solarxr_ipc_socket_is_connected(&fixture.socket));
	}
	check(solarxr_ipc_socket_reconnect(&fixture.socket, fixture.address));
	fixture.peer = accept(fixture.listener, NULL, NULL);
	check(fixture.peer >= 0);
	check_fresh_frame(&fixture);
	fixture_destroy(&fixture);
}

static void test_descriptor_cleanup(void)
{
	struct fixture fixture;
	fixture_init(&fixture);
	for (unsigned iteration = 0; iteration < 100; ++iteration)
	{
		int previous = atomic_load(&fixture.socket.ipc_handle);
		check(close(fixture.peer) == 0);
		check(!solarxr_ipc_socket_wait_timeout(&fixture.socket, 0));
		check(solarxr_ipc_socket_reconnect(&fixture.socket, fixture.address));
		errno = 0;
		check(fcntl(previous, F_GETFD) == -1 && errno == EBADF);
		fixture.peer = accept(fixture.listener, NULL, NULL);
		check(fixture.peer >= 0);
		check_fresh_frame(&fixture);
	}
	fixture_destroy(&fixture);
}

static void test_shutdown(void)
{
	struct fixture fixture;
	fixture_init(&fixture);
	solarxr_ipc_socket_destroy(&fixture.socket);
	check(!solarxr_ipc_socket_reconnect(&fixture.socket, fixture.address));
	check(!solarxr_ipc_socket_is_connected(&fixture.socket));
	fixture_destroy(&fixture);
}

int main(int argc, char ** argv)
{
	if (argc == 1 || strcmp(argv[1], "partial") == 0)
	{
		for (unsigned failed_attempts = 0; failed_attempts <= 3; failed_attempts += 3)
		{
			test_partial_frame(false, failed_attempts);
			test_partial_frame(true, failed_attempts);
		}
	}
	if (argc == 1 || strcmp(argv[1], "descriptors") == 0)
		test_descriptor_cleanup();
	if (argc == 1 || strcmp(argv[1], "shutdown") == 0)
		test_shutdown();
	puts("SolarXR partial-frame recovery, descriptor cleanup and shutdown passed");
	return EXIT_SUCCESS;
}
