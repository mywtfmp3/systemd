/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

/***
  This file is part of systemd.

  Copyright (C) 2013 Intel Corporation. All rights reserved.
  Copyright (C) 2014 Tom Gundersen

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include <netinet/if_ether.h>
#include <assert.h>
#include <errno.h>

#include "sd-event.h"
#include "event-util.h"

#include "sd-dhcp-server.h"
#include "dhcp-server-internal.h"

static void test_basic(sd_event *event) {
        _cleanup_dhcp_server_unref_ sd_dhcp_server *server = NULL;
        struct in_addr address_lo = {
                .s_addr = htonl(INADDR_LOOPBACK),
        };
        struct in_addr address_any = {
                .s_addr = htonl(INADDR_ANY),
        };

        /* attach to loopback interface */
        assert_se(sd_dhcp_server_new(&server, 1) >= 0);
        assert_se(server);

        assert_se(sd_dhcp_server_attach_event(server, event, 0) >= 0);
        assert_se(sd_dhcp_server_attach_event(server, event, 0) == -EBUSY);
        assert_se(sd_dhcp_server_get_event(server) == event);
        assert_se(sd_dhcp_server_detach_event(server) >= 0);
        assert_se(!sd_dhcp_server_get_event(server));
        assert_se(sd_dhcp_server_attach_event(server, NULL, 0) >= 0);
        assert_se(sd_dhcp_server_attach_event(server, NULL, 0) == -EBUSY);

        assert_se(sd_dhcp_server_ref(server) == server);
        assert_se(!sd_dhcp_server_unref(server));

        assert_se(sd_dhcp_server_start(server) == -EUNATCH);
        assert_se(sd_dhcp_server_set_address(server, &address_any) == -EINVAL);
        assert_se(sd_dhcp_server_set_address(server, &address_lo) >= 0);
        assert_se(sd_dhcp_server_set_address(server, &address_lo) == -EBUSY);

        assert_se(sd_dhcp_server_set_lease_pool(server, &address_any, 1) == -EINVAL);
        assert_se(sd_dhcp_server_set_lease_pool(server, &address_lo, 0) == -EINVAL);
        assert_se(sd_dhcp_server_set_lease_pool(server, &address_lo, 1) >= 0);
        assert_se(sd_dhcp_server_set_lease_pool(server, &address_lo, 1) == -EBUSY);

        assert_se(sd_dhcp_server_start(server) >= 0);
        assert_se(sd_dhcp_server_start(server) == -EBUSY);
        assert_se(sd_dhcp_server_stop(server) >= 0);
        assert_se(sd_dhcp_server_stop(server) >= 0);
        assert_se(sd_dhcp_server_start(server) >= 0);
}

static void test_message_handler(void) {
        _cleanup_dhcp_server_unref_ sd_dhcp_server *server = NULL;
        struct {
                DHCPMessage message;
                struct {
                        uint8_t code;
                        uint8_t length;
                        uint8_t type;
                } _packed_ option_type;
                struct {
                        uint8_t code;
                        uint8_t length;
                        be32_t address;
                } _packed_ option_requested_ip;
                struct {
                        uint8_t code;
                        uint8_t length;
                        be32_t address;
                } _packed_ option_server_id;
                uint8_t end;
        } _packed_ test = {
                .message.op = BOOTREQUEST,
                .message.htype = ARPHRD_ETHER,
                .message.hlen = ETHER_ADDR_LEN,
                .message.xid = htobe32(0x12345678),
                .message.chaddr = { 'A', 'B', 'C', 'D', 'E', 'F' },
                .option_type.code = DHCP_OPTION_MESSAGE_TYPE,
                .option_type.length = 1,
                .option_type.type = DHCP_DISCOVER,
                .end = DHCP_OPTION_END,
        };
        struct in_addr address_lo = {
                .s_addr = htonl(INADDR_LOOPBACK),
        };

        assert_se(sd_dhcp_server_new(&server, 1) >= 0);
        assert_se(sd_dhcp_server_set_address(server, &address_lo) >= 0);
        assert_se(sd_dhcp_server_attach_event(server, NULL, 0) >= 0);
        assert_se(sd_dhcp_server_start(server) >= 0);

        assert_se(dhcp_server_handle_message(server, (DHCPMessage*)&test, sizeof(test)) == 0);
        assert_se(sd_dhcp_server_set_lease_pool(server, &address_lo, 10) >= 0);
        assert_se(dhcp_server_handle_message(server, (DHCPMessage*)&test, sizeof(test)) == DHCP_OFFER);

        test.end = 0;
        /* TODO, shouldn't this fail? */
        assert_se(dhcp_server_handle_message(server, (DHCPMessage*)&test, sizeof(test)) == DHCP_OFFER);
        test.end = DHCP_OPTION_END;
        assert_se(dhcp_server_handle_message(server, (DHCPMessage*)&test, sizeof(test)) == DHCP_OFFER);

        test.option_type.code = 0;
        test.option_type.length = 0;
        test.option_type.type = 0;
        assert_se(dhcp_server_handle_message(server, (DHCPMessage*)&test, sizeof(test)) == 0);
        test.option_type.code = DHCP_OPTION_MESSAGE_TYPE;
        test.option_type.length = 1;
        test.option_type.type = DHCP_DISCOVER;
        assert_se(dhcp_server_handle_message(server, (DHCPMessage*)&test, sizeof(test)) == DHCP_OFFER);

        test.message.op = 0;
        assert_se(dhcp_server_handle_message(server, (DHCPMessage*)&test, sizeof(test)) == 0);
        test.message.op = BOOTREQUEST;
        assert_se(dhcp_server_handle_message(server, (DHCPMessage*)&test, sizeof(test)) == DHCP_OFFER);

        test.message.htype = 0;
        assert_se(dhcp_server_handle_message(server, (DHCPMessage*)&test, sizeof(test)) == 0);
        test.message.htype = ARPHRD_ETHER;
        assert_se(dhcp_server_handle_message(server, (DHCPMessage*)&test, sizeof(test)) == DHCP_OFFER);

        test.message.hlen = 0;
        assert_se(dhcp_server_handle_message(server, (DHCPMessage*)&test, sizeof(test)) == 0);
        test.message.hlen = ETHER_ADDR_LEN;
        assert_se(dhcp_server_handle_message(server, (DHCPMessage*)&test, sizeof(test)) == DHCP_OFFER);

        test.option_type.type = DHCP_REQUEST;
        assert_se(dhcp_server_handle_message(server, (DHCPMessage*)&test, sizeof(test)) == 0);
        test.option_requested_ip.code = DHCP_OPTION_REQUESTED_IP_ADDRESS;
        test.option_requested_ip.length = 4;
        test.option_requested_ip.address = htobe32(0x12345678);
        assert_se(dhcp_server_handle_message(server, (DHCPMessage*)&test, sizeof(test)) == DHCP_NAK);
        test.option_server_id.code = DHCP_OPTION_SERVER_IDENTIFIER;
        test.option_server_id.length = 4;
        test.option_server_id.address = htobe32(INADDR_LOOPBACK);
        test.option_requested_ip.address = htobe32(INADDR_LOOPBACK + 3);
        assert_se(dhcp_server_handle_message(server, (DHCPMessage*)&test, sizeof(test)) == DHCP_ACK);
        test.option_server_id.address = htobe32(0x12345678);
        test.option_requested_ip.address = htobe32(INADDR_LOOPBACK + 3);
        assert_se(dhcp_server_handle_message(server, (DHCPMessage*)&test, sizeof(test)) == 0);
        test.option_server_id.address = htobe32(INADDR_LOOPBACK);
        test.option_requested_ip.address = htobe32(INADDR_LOOPBACK + 30);
        assert_se(dhcp_server_handle_message(server, (DHCPMessage*)&test, sizeof(test)) == 0);
}

int main(int argc, char *argv[]) {
        _cleanup_event_unref_ sd_event *e;

        log_set_max_level(LOG_DEBUG);
        log_parse_environment();
        log_open();

        assert_se(sd_event_new(&e) >= 0);

        test_basic(e);
        test_message_handler();

        return 0;
}
