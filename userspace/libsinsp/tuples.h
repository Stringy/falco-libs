// SPDX-License-Identifier: Apache-2.0
/*
Copyright (C) 2023 The Falco Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/

#pragma once

#include <stdint.h>
#include <string>

/** @defgroup state State management
 *  @{
 */

/*!
    \brief An IPv4 tuple.
*/
union ipv4tuple {
	struct {
		uint32_t m_sip;     ///< Source (i.e. client) address.
		uint32_t m_dip;     ///< Destination (i.e. server) address.
		uint16_t m_sport;   ///< Source (i.e. client) port.
		uint16_t m_dport;   ///< Destination (i.e. server) port.
		uint8_t m_l4proto;  ///< Layer 4 protocol (e.g. TCP, UDP...).
	} m_fields;
	uint8_t m_all[13];  ///< The fields as a raw array ob bytes. Used for hashing.
};

/*!
    \brief An IPv4 network.
*/
struct ipv4net {
	uint32_t m_ip;       ///< IP addr
	uint32_t m_netmask;  ///< Subnet mask
};

struct ipv6addr {
	ipv6addr() = default;
	ipv6addr(const std::string &str_addr);
	uint32_t m_b[4];

	bool operator==(const ipv6addr &other) const;
	bool operator!=(const ipv6addr &other) const;
	bool operator<(const ipv6addr &other) const;
	bool in_subnet(const ipv6addr &other) const;

	static struct ipv6addr empty_address;
};

class ipv6net {
private:
	ipv6addr m_addr;
	uint32_t m_mask_len_bytes;
	uint32_t m_mask_tail_bits;
	void init(const std::string &str);

public:
	ipv6net(const std::string &str);
	bool in_cidr(const ipv6addr &other) const;
};

/*!
    \brief An IPv6 tuple.
*/
union ipv6tuple {
	struct {
		ipv6addr m_sip;     ///< source (i.e. client) address.
		ipv6addr m_dip;     ///< destination (i.e. server) address.
		uint16_t m_sport;   ///< source (i.e. client) port.
		uint16_t m_dport;   ///< destination (i.e. server) port.
		uint8_t m_l4proto;  ///< Layer 4 protocol (e.g. TCP, UDP...)
	} m_fields;
	uint8_t m_all[37];  ///< The fields as a raw array ob bytes. Used for hashing.
};

/*!
    \brief An IPv4 server address.
*/
struct ipv4serverinfo {
	uint32_t m_ip;      ///< address
	uint16_t m_port;    ///< port
	uint8_t m_l4proto;  ///< IP protocol
};

/*!
    \brief An IPv6 server address.
*/
struct ipv6serverinfo {
	ipv6addr m_ip;      ///< address
	uint16_t m_port;    ///< port
	uint8_t m_l4proto;  ///< IP protocol
};

/*!
    \brief A unix socket tuple.
*/
union unix_tuple {
	struct {
		uint64_t m_source;  ///< source OS pointer.
		uint64_t m_dest;    ///< destination OS pointer.
	} m_fields;
	uint8_t m_all[16];  ///< The fields as a raw array ob bytes. Used for hashing.
};

/*@}*/
