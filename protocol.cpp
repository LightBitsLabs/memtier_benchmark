/*
 * Copyright (C) 2011-2017 Redis Labs Ltd.
 *
 * This file is part of memtier_benchmark.
 *
 * memtier_benchmark is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * memtier_benchmark is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with memtier_benchmark.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef HAVE_ASSERT_H
#include <assert.h>
#endif

#include "protocol.h"
#include "memtier_benchmark.h"
#include "libmemcached_protocol/binary.h"

/////////////////////////////////////////////////////////////////////////

abstract_protocol::abstract_protocol() :
    m_read_buf(NULL), m_write_buf(NULL), m_keep_value(false)
{    
}

abstract_protocol::~abstract_protocol()
{
}

void abstract_protocol::set_buffers(struct evbuffer* read_buf, struct evbuffer* write_buf)
{
    m_read_buf = read_buf;
    m_write_buf = write_buf;
}

void abstract_protocol::set_keep_value(bool flag)
{
    m_keep_value = flag;
}

/////////////////////////////////////////////////////////////////////////

protocol_response::protocol_response()
    : m_status(NULL), m_value(NULL), m_value_len(0), m_hits(0), m_error(false)
{
}

protocol_response::~protocol_response()
{
    clear();
}

void protocol_response::set_error(bool error)
{
    m_error = error;
}

bool protocol_response::is_error(void)
{
    return m_error;
}

void protocol_response::set_status(const char* status)
{
    if (m_status != NULL)
        free((void *)m_status);
    m_status = status;
}

const char* protocol_response::get_status(void)
{
    return m_status;
}

void protocol_response::set_value(const char* value, unsigned int value_len, const char* key, unsigned int key_len)
{
    key_val_node node(value, value_len, key, key_len);
    m_values.push_back(node);
    m_value_len = value_len;
}

const char* protocol_response::get_value(unsigned int *value_len, const char** key, unsigned int *key_len)
{
    assert(value_len != NULL);
    *value_len = m_values.front().value_len;
    *key_len = m_values.front().key_len;
    *key = m_values.front().key;
    const char* value = m_values.front().value;
    m_values.pop_front();
    return value;
}

unsigned int protocol_response::get_values_count()
{
    return m_values.size();
}

void protocol_response::set_latency(unsigned int latency)
{
    m_latencies.push_back(latency);
}

unsigned int protocol_response::get_latency()
{
    unsigned int latency = m_latencies.front();
    m_latencies.pop_front();
    return latency;
}

unsigned int protocol_response::get_latencies_count()
{
    return m_latencies.size();
}
void protocol_response::set_total_len(unsigned int total_len)
{
    m_total_len = total_len;
}

unsigned int protocol_response::get_total_len(void)
{    
    return m_total_len;
}

void protocol_response::incr_hits(void)
{
    m_hits++;
}

unsigned int protocol_response::get_hits(void)
{
    return m_hits;
}

static bool deleteValues(key_val_node node) {
    if (node.value != NULL)
        free((void *)node.value);
    if (node.key != NULL)
        free((void *)node.key);
        return true;
}

void protocol_response::clear(void)
{
    if (m_status != NULL) {
        free((void *)m_status);
        m_status = NULL;
    }
    if (!m_values.empty()) {
        m_values.remove_if(deleteValues);
    }
	if (!m_latencies.empty()) {
		m_latencies.clear();
	}
    m_value_len = 0;
    m_total_len = 0;
    m_hits = 0;
    m_error = 0;
}

/////////////////////////////////////////////////////////////////////////

class redis_protocol : public abstract_protocol {
protected:
    enum response_state { rs_initial, rs_read_bulk };
    response_state m_response_state;
    unsigned int m_bulk_len;
    size_t m_response_len;
public:
    redis_protocol() : m_response_state(rs_initial), m_bulk_len(0), m_response_len(0) { }
    virtual redis_protocol* clone(void) { return new redis_protocol(); }
    virtual int select_db(int db);
    virtual int authenticate(const char *credentials);
    virtual int write_command_set(const char *key, int key_len, const char *value, int value_len, int expiry, unsigned int offset);
    virtual int write_command_get(const char *key, int key_len, unsigned int offset);
    virtual int write_command_get_key(const char *key, int key_len, unsigned int offset);
    virtual int write_command_multi_get(const keylist *keylist);
    virtual int write_command_wait(unsigned int num_slaves, unsigned int timeout);
    virtual int parse_response(unsigned int latency);
};

int redis_protocol::select_db(int db)
{
    int size = 0;
    char db_str[20];

    snprintf(db_str, sizeof(db_str)-1, "%d", db);
    size = evbuffer_add_printf(m_write_buf,
        "*2\r\n"
        "$6\r\n"
        "SELECT\r\n"
        "$%u\r\n"
        "%s\r\n",
        (unsigned int)strlen(db_str), db_str);
    return size;
}

int redis_protocol::authenticate(const char *credentials)
{
    int size = 0;
    assert(credentials != NULL);

    size = evbuffer_add_printf(m_write_buf,
        "*2\r\n"
        "$4\r\n"
        "AUTH\r\n"
        "$%u\r\n"
        "%s\r\n",
        (unsigned int)strlen(credentials), credentials);
    return size;
}

int redis_protocol::write_command_set(const char *key, int key_len, const char *value, int value_len, int expiry, unsigned int offset)
{
    assert(key != NULL);
    assert(key_len > 0);
    assert(value != NULL);
    assert(value_len > 0);
    int size = 0;
    
    if (!expiry && !offset) {
        size = evbuffer_add_printf(m_write_buf,
            "*3\r\n"
            "$3\r\n"
            "SET\r\n"
            "$%u\r\n", key_len);
        evbuffer_add(m_write_buf, key, key_len);
        size += key_len;
        size += evbuffer_add_printf(m_write_buf,
            "\r\n"
            "$%u\r\n", value_len);
    } else if(offset) {
        char offset_str[30];
        snprintf(offset_str, sizeof(offset_str)-1, "%u", offset);

        size = evbuffer_add_printf(m_write_buf,
            "*4\r\n"
            "$8\r\n"
            "SETRANGE\r\n"
            "$%u\r\n", key_len);
        evbuffer_add(m_write_buf, key, key_len);
        size += key_len;
        size += evbuffer_add_printf(m_write_buf,
            "\r\n"
            "$%u\r\n"
            "%s\r\n"
            "$%u\r\n", (unsigned int) strlen(offset_str), offset_str, value_len);
    } else {
        char expiry_str[30];
        snprintf(expiry_str, sizeof(expiry_str)-1, "%u", expiry);
        
        size = evbuffer_add_printf(m_write_buf,
            "*4\r\n"
            "$5\r\n"
            "SETEX\r\n"
            "$%u\r\n", key_len);
        evbuffer_add(m_write_buf, key, key_len);
        size += key_len;
        size += evbuffer_add_printf(m_write_buf,
            "\r\n"
            "$%u\r\n"
            "%s\r\n"
            "$%u\r\n", (unsigned int) strlen(expiry_str), expiry_str, value_len);
    }
    evbuffer_add(m_write_buf, value, value_len);
    evbuffer_add(m_write_buf, "\r\n", 2);
    size += value_len + 2;

    return size;
}

int redis_protocol::write_command_multi_get(const keylist *keylist)
{
    fprintf(stderr, "error: multi get not implemented for redis yet!\n");
    assert(0);
}

int redis_protocol::write_command_get(const char *key, int key_len, unsigned int offset)
{
    assert(key != NULL);
    assert(key_len > 0);
    int size = 0;
    
    if (!offset) {
        size = evbuffer_add_printf(m_write_buf,
            "*2\r\n"
            "$3\r\n"
            "GET\r\n"
            "$%u\r\n", key_len);
        evbuffer_add(m_write_buf, key, key_len);
        evbuffer_add(m_write_buf, "\r\n", 2);        
        size += key_len + 2;
    } else {
        char offset_str[30];
        snprintf(offset_str, sizeof(offset_str)-1, "%u", offset);

        size = evbuffer_add_printf(m_write_buf,
            "*4\r\n"
            "$8\r\n"
            "GETRANGE\r\n"
            "$%u\r\n", key_len);
        evbuffer_add(m_write_buf, key, key_len);
        size += key_len;
        size += evbuffer_add_printf(m_write_buf,
            "\r\n"
            "$%u\r\n"
            "%s\r\n"
            "$2\r\n"
            "-1\r\n", (unsigned int) strlen(offset_str), offset_str);        
    }

    return size;
}

int redis_protocol::write_command_get_key(const char *key, int key_len, unsigned int offset)
{
    fprintf(stderr, "error: get key is not supported for redis!\n");
	assert(0);
}

/*
 * Utility function to get the number of digits in a number
 */
static int get_number_length(unsigned int num)
{
    if (num < 10) return 1;
    if (num < 100) return 2;
    if (num < 1000) return 3;
    if (num < 10000) return 4;
    if (num < 100000) return 5;
    if (num < 1000000) return 6;
    if (num < 10000000) return 7;
    if (num < 100000000) return 8;
    if (num < 1000000000) return 9;
    return 10;
}

int redis_protocol::write_command_wait(unsigned int num_slaves, unsigned int timeout)
{
    int size = 0;
    size = evbuffer_add_printf(m_write_buf,
                               "*3\r\n"
                               "$4\r\n"
                               "WAIT\r\n"
                               "$%u\r\n"
                               "%u\r\n"
                               "$%u\r\n"
                               "%u\r\n",
                               get_number_length(num_slaves), num_slaves,
                               get_number_length(timeout), timeout);
    return size;
}

int redis_protocol::parse_response(unsigned int latency)
{
    char *line;

    while (true) {
        switch (m_response_state) {
            case rs_initial:                
                line = evbuffer_readln(m_read_buf, &m_response_len, EVBUFFER_EOL_CRLF_STRICT);
                if (line == NULL)
                    return 0;   // maybe we didn't get it yet?
                m_response_len += 2;    // count CRLF
        
                // todo: support multi-bulk reply
                if (line[0] == '*') {
                    benchmark_debug_log("multi-bulk replies not currently supported.\n");
                    free(line);
                    return -1;
                }

                // clear last response
                m_last_response.clear();
                m_last_response.set_latency(latency);

                // bulk?
                if (line[0] == '$') {
                    int len = strtol(line + 1, NULL, 10);
                    if (len == -1) {
                        m_last_response.set_status(line);
                        return 1;
                    }

                    m_bulk_len = (unsigned int) len;
                    m_response_state = rs_read_bulk;
                    m_last_response.set_status(line);
                    continue;
                } else if (line[0] == '+' || line[0] == '-' || line[0] == ':') {
                    m_last_response.set_status(line);
                    m_last_response.set_total_len(m_response_len);
                    if (line[0] == '-')
                        m_last_response.set_error(true);
                    return 1;
                } else {
                    benchmark_debug_log("unsupported response: '%s'.\n", line);
                    free(line);
                    return -1;
                }

                break;
            case rs_read_bulk:
                if (evbuffer_get_length(m_read_buf) >= m_bulk_len + 2) {
                    if (m_keep_value && m_bulk_len > 0) {
                        char *bulk_value = (char *) malloc(m_bulk_len);
                        assert(bulk_value != NULL);
                            
                        int ret = evbuffer_remove(m_read_buf, bulk_value, m_bulk_len);
                        assert(ret != -1);

                        // drain CRLF
                        ret = evbuffer_drain(m_read_buf, 2);
                        assert(ret != -1);

                        m_last_response.set_value(bulk_value, m_bulk_len, NULL, 0);
                    } else {
                        int ret = evbuffer_drain(m_read_buf, m_bulk_len + 2);
                        assert(ret != -1);
                    }

                    m_response_state = rs_initial;
                    m_last_response.set_total_len(m_response_len + m_bulk_len + 2);
                    if (m_bulk_len > 0)
                        m_last_response.incr_hits();
                    return 1;
                } else {
                    return 0;
                }
                break;
            default:
                return -1;
        }
    }

    return -1;
}

/////////////////////////////////////////////////////////////////////////

class memcache_text_protocol : public abstract_protocol {
protected:
    enum response_state { rs_initial, rs_read_section, rs_read_value, rs_read_end };
    response_state m_response_state;
    unsigned int m_value_len;
    size_t m_response_len;
public:
    memcache_text_protocol() : m_response_state(rs_initial), m_value_len(0), m_response_len(0) { }
    virtual memcache_text_protocol* clone(void) { return new memcache_text_protocol(); }
    virtual int select_db(int db);
    virtual int authenticate(const char *credentials);
    virtual int write_command_set(const char *key, int key_len, const char *value, int value_len, int expiry, unsigned int offset);
    virtual int write_command_get(const char *key, int key_len, unsigned int offset);
    virtual int write_command_get_key(const char *key, int key_len, unsigned int offset);
    virtual int write_command_multi_get(const keylist *keylist);
    virtual int write_command_wait(unsigned int num_slaves, unsigned int timeout);
    virtual int parse_response(unsigned int latency);
};

int memcache_text_protocol::select_db(int db)
{
    assert(0);
}

int memcache_text_protocol::authenticate(const char *credentials)
{
    assert(0);
}

int memcache_text_protocol::write_command_set(const char *key, int key_len, const char *value, int value_len, int expiry, unsigned int offset)
{
    assert(key != NULL);
    assert(key_len > 0);
    assert(value != NULL);
    assert(value_len > 0);
    int size = 0;
    
    size = evbuffer_add_printf(m_write_buf,
        "set %.*s 0 %u %u\r\n", key_len, key, expiry, value_len);
    evbuffer_add(m_write_buf, value, value_len);
    evbuffer_add(m_write_buf, "\r\n", 2);
    size += value_len + 2;

    return size;
}

int memcache_text_protocol::write_command_get(const char *key, int key_len, unsigned int offset)
{
    assert(key != NULL);
    assert(key_len > 0);
    int size = 0;

    size = evbuffer_add_printf(m_write_buf,
        "get %.*s\r\n", key_len, key);
    return size;
}

int memcache_text_protocol::write_command_get_key(const char *key, int key_len, unsigned int offset)
{
	// in memecache text protocol get also returns key
    return this->write_command_get(key, key_len, offset);
}

int memcache_text_protocol::write_command_multi_get(const keylist *keylist)
{
    assert(keylist != NULL);
    assert(keylist->get_keys_count() > 0);

    int n = 0;
    int size = 0;

    n = evbuffer_add(m_write_buf, "get", 3);
    assert(n != -1);
    size = 3;
    
    for (unsigned int i = 0; i < keylist->get_keys_count(); i++) {
        const char *key;
        unsigned int key_len;
        
        n = evbuffer_add(m_write_buf, " ", 1);
        assert(n != -1);
        size++;

        key = keylist->get_key(i, &key_len);
        assert(key != NULL);
        
        n = evbuffer_add(m_write_buf, key, key_len);
        assert(n != -1);
        size += key_len;
    }

    n = evbuffer_add(m_write_buf, "\r\n", 2);
    assert(n != -1);
    size += 2;

    return size;
}

int memcache_text_protocol::write_command_wait(unsigned int num_slaves, unsigned int timeout)
{
    fprintf(stderr, "error: WAIT command not implemented for memcache!\n");
    assert(0);
}

int memcache_text_protocol::parse_response(unsigned int latency)
{
    char *line;
    size_t tmplen;
    
    while (true) {
        switch (m_response_state) {
            case rs_initial:
                m_last_response.clear();
                m_response_state = rs_read_section;
                m_response_len = 0;
                break;                
                
            case rs_read_section:
                line = evbuffer_readln(m_read_buf, &tmplen, EVBUFFER_EOL_CRLF_STRICT);
                if (!line)
                    return 0;

                m_response_len += tmplen + 2;   // For CRLF
                if (m_last_response.get_status() == NULL) {
                    m_last_response.set_status(line);
                }
                m_last_response.set_total_len((unsigned int) m_response_len);   // for now...                    
                
                if (memcmp(line, "VALUE", 5) == 0) {
                    char prefix[50];
                    char key[256];
                    unsigned int flags;
                    unsigned int cas;

                    int res = sscanf(line, "%s %s %u %u %u", prefix, key, &flags, &m_value_len, &cas);
                    if (res < 4|| res > 5) {
                        benchmark_debug_log("unexpected VALUE response: %s\n", line);
                        if (m_last_response.get_status() != line)
                            free(line);
                        return -1;
                    }

                    m_last_response.set_latency(latency);
                    m_response_state = rs_read_value;
                    continue;
                } else if (memcmp(line, "END", 3) == 0 ||
                           memcmp(line, "STORED", 6) == 0) {
                    if (m_last_response.get_status() != line)
                        free(line);
                    m_response_state = rs_read_end;
                } else {
                    m_last_response.set_error(true);
                    benchmark_debug_log("unknown response: %s\n", line);
                    return -1;
                }
                m_last_response.set_latency(latency);
                break;
                
            case rs_read_value:                
                if (evbuffer_get_length(m_read_buf) >= m_value_len + 2) {
                    if (m_keep_value) {
                        char *value = (char *) malloc(m_value_len);
                        assert(value != NULL);
                            
                        int ret = evbuffer_remove(m_read_buf, value, m_value_len);
                        assert((unsigned int) ret == 0);

                        m_last_response.set_value(value, m_value_len, NULL, 0);
                    } else {
                        int ret = evbuffer_drain(m_read_buf, m_value_len);
                        assert((unsigned int) ret == 0);
                    }

                    int ret = evbuffer_drain(m_read_buf, 2);
                    assert((unsigned int) ret == 0);

                    m_last_response.incr_hits();
                    m_response_len += m_value_len + 2;
                    m_response_state = rs_read_section;
                } else {
                    return 0;
                }
                break;
            case rs_read_end:
                m_response_state = rs_initial;
                return 1;
                
            default:
                benchmark_debug_log("unknown response state %d.\n", m_response_state);
                return -1;
        }
    }

    return -1;
}

/////////////////////////////////////////////////////////////////////////

class memcache_binary_protocol : public abstract_protocol {
protected:
    enum response_state { rs_initial, rs_multi_initial, rs_read_body };
    response_state m_response_state;
    protocol_binary_response_no_extras m_response_hdr;
    size_t m_response_len;

    const char* status_text(void);
public:
    memcache_binary_protocol() : m_response_state(rs_initial), m_response_len(0) { }
    virtual memcache_binary_protocol* clone(void) { return new memcache_binary_protocol(); }
    virtual int select_db(int db);
    virtual int authenticate(const char *credentials);
    virtual int write_command_set(const char *key, int key_len, const char *value, int value_len, int expiry, unsigned int offset);
    virtual int write_command_get(const char *key, int key_len, unsigned int offset);
    virtual int write_command_get_key(const char *key, int key_len, unsigned int offset);
    virtual int write_command_multi_get(const keylist *keylist);
    virtual int write_command_wait(unsigned int num_slaves, unsigned int timeout);
    virtual int parse_response(unsigned int latency);
};

int memcache_binary_protocol::select_db(int db)
{
    assert(0);
}

int memcache_binary_protocol::authenticate(const char *credentials)
{
    protocol_binary_request_no_extras req;
    char nullbyte = '\0';
    const char mechanism[] = "PLAIN";
    int mechanism_len = sizeof(mechanism) - 1;
    const char *colon;
    const char *user;
    int user_len;
    const char *passwd;
    int passwd_len;

    assert(credentials != NULL);
    colon = strchr(credentials, ':');
    assert(colon != NULL);

    user = credentials;
    user_len = colon - user;
    passwd = colon + 1;
    passwd_len = strlen(passwd);
    
    memset(&req, 0, sizeof(req));
    req.message.header.request.magic = PROTOCOL_BINARY_REQ;
    req.message.header.request.opcode = PROTOCOL_BINARY_CMD_SASL_AUTH;
    req.message.header.request.keylen = htons(mechanism_len);
    req.message.header.request.datatype = PROTOCOL_BINARY_RAW_BYTES;
    req.message.header.request.bodylen = htonl(mechanism_len + user_len + passwd_len + 2);
    evbuffer_add(m_write_buf, &req, sizeof(req));
    evbuffer_add(m_write_buf, mechanism, mechanism_len);
    evbuffer_add(m_write_buf, &nullbyte, 1);
    evbuffer_add(m_write_buf, user, user_len);
    evbuffer_add(m_write_buf, &nullbyte, 1);
    evbuffer_add(m_write_buf, passwd, passwd_len);

    return sizeof(req) + user_len + passwd_len + 2 + sizeof(mechanism) - 1;
}

int memcache_binary_protocol::write_command_set(const char *key, int key_len, const char *value, int value_len, int expiry, unsigned int offset)
{
    assert(key != NULL);
    assert(key_len > 0);
    assert(value != NULL);
    assert(value_len > 0);

    protocol_binary_request_set req;

    memset(&req, 0, sizeof(req));
    req.message.header.request.magic = PROTOCOL_BINARY_REQ;
    req.message.header.request.opcode = PROTOCOL_BINARY_CMD_SET;
    req.message.header.request.keylen = htons(key_len);
    req.message.header.request.datatype = PROTOCOL_BINARY_RAW_BYTES;
    req.message.header.request.bodylen = htonl(sizeof(req.message.body) + value_len + key_len);
    req.message.header.request.extlen = sizeof(req.message.body);
    req.message.body.expiration = htonl(expiry);

    evbuffer_add(m_write_buf, &req, sizeof(req));
    evbuffer_add(m_write_buf, key, key_len);
    evbuffer_add(m_write_buf, value, value_len);

    return sizeof(req) + key_len + value_len;
}

int memcache_binary_protocol::write_command_get(const char *key, int key_len, unsigned int offset)
{
    assert(key != NULL);
    assert(key_len > 0);

    protocol_binary_request_get req;

    memset(&req, 0, sizeof(req));
    req.message.header.request.magic = PROTOCOL_BINARY_REQ;
    req.message.header.request.opcode = PROTOCOL_BINARY_CMD_GET;
    req.message.header.request.keylen = htons(key_len);
    req.message.header.request.datatype = PROTOCOL_BINARY_RAW_BYTES;
    req.message.header.request.bodylen = htonl(key_len);
    req.message.header.request.extlen = 0;

    evbuffer_add(m_write_buf, &req, sizeof(req));
    evbuffer_add(m_write_buf, key, key_len);

    return sizeof(req) + key_len;
}

int memcache_binary_protocol::write_command_get_key(const char *key, int key_len, unsigned int offset)
{
    assert(key != NULL);
    assert(key_len > 0);

    protocol_binary_request_get req;

    memset(&req, 0, sizeof(req));
    req.message.header.request.magic = PROTOCOL_BINARY_REQ;
    req.message.header.request.opcode = PROTOCOL_BINARY_CMD_GETK;
    req.message.header.request.keylen = htons(key_len);
    req.message.header.request.datatype = PROTOCOL_BINARY_RAW_BYTES;
    req.message.header.request.bodylen = htonl(key_len);
    req.message.header.request.extlen = 0;

    evbuffer_add(m_write_buf, &req, sizeof(req));
    evbuffer_add(m_write_buf, key, key_len);

    return sizeof(req) + key_len;
}

int memcache_binary_protocol::write_command_multi_get(const keylist *keylist)
{
    int cmd_size = 0;
    unsigned int last_key_len = 0;
    const char *last_key;
    // should we implement this with GETQ?
    for (unsigned int i=0; i < keylist->get_keys_count() - 1; i++) {
        unsigned int key_len = 0;
        const char *key;

        key =  keylist->get_key(i, &key_len);
        protocol_binary_request_getkq req;

        memset(&req, 0, sizeof(req));
        req.message.header.request.magic = PROTOCOL_BINARY_REQ;
        req.message.header.request.opcode = PROTOCOL_BINARY_CMD_GETKQ;
        req.message.header.request.keylen = htons(key_len);
        req.message.header.request.datatype = PROTOCOL_BINARY_RAW_BYTES;
        req.message.header.request.bodylen = htonl(key_len);
        req.message.header.request.extlen = 0;

        evbuffer_add(m_write_buf, &req, sizeof(req));
        evbuffer_add(m_write_buf, key, key_len);

        cmd_size += sizeof(req) + key_len;
    }

    last_key = keylist->get_key(keylist->get_keys_count() - 1, &last_key_len);
    protocol_binary_request_getk req;

    memset(&req, 0, sizeof(req));
    req.message.header.request.magic = PROTOCOL_BINARY_REQ;
    req.message.header.request.opcode = PROTOCOL_BINARY_CMD_GETK;
    req.message.header.request.keylen = htons(last_key_len);
    req.message.header.request.datatype = PROTOCOL_BINARY_RAW_BYTES;
    req.message.header.request.bodylen = htonl(last_key_len);
    req.message.header.request.extlen = 0;

    evbuffer_add(m_write_buf, &req, sizeof(req));
    evbuffer_add(m_write_buf, last_key, last_key_len);

    return sizeof(req) + last_key_len + cmd_size;
}

const char* memcache_binary_protocol::status_text(void)
{
    int status;
    static const char* status_str_00[] = {
        "PROTOCOL_BINARY_RESPONSE_SUCCESS",
        "PROTOCOL_BINARY_RESPONSE_KEY_ENOENT",
        "PROTOCOL_BINARY_RESPONSE_KEY_EEXISTS",
        "PROTOCOL_BINARY_RESPONSE_E2BIG",
        "PROTOCOL_BINARY_RESPONSE_EINVAL",
        "PROTOCOL_BINARY_RESPONSE_NOT_STORED",
        "PROTOCOL_BINARY_RESPONSE_DELTA_BADVAL",
        "PROTOCOL_BINARY_RESPONSE_NOT_MY_VBUCKET",
    };
    static const char* status_str_20[] = {
        "PROTOCOL_BINARY_RESPONSE_AUTH_ERROR",
        "PROTOCOL_BINARY_RESPONSE_AUTH_CONTINUE"
    };
    static const char* status_str_80[] = {
        NULL,
        "PROTOCOL_BINARY_RESPONSE_UNKNOWN_COMMAND",
        "PROTOCOL_BINARY_RESPONSE_ENOMEM",
        "PROTOCOL_BINARY_RESPONSE_NOT_SUPPORTED",
        "PROTOCOL_BINARY_RESPONSE_EINTERNAL",
        "PROTOCOL_BINARY_RESPONSE_EBUSY",
        "PROTOCOL_BINARY_RESPONSE_ETMPFAIL"
    };

    status = ntohs(m_response_hdr.message.header.response.status);
    if (status <= 0x07) {
        return status_str_00[status];
    } else if (status >= 0x20 && status <= 0x21) {
        return status_str_20[status - 0x20];
    } else if (status >= 0x80 && status <= 0x86) {
        return status_str_80[status - 0x80];
    } else {
        return NULL;
    }
}

int memcache_binary_protocol::write_command_wait(unsigned int num_slaves, unsigned int timeout)
{
    fprintf(stderr, "error: WAIT command not implemented for memcache!\n");
    assert(0);
}

int memcache_binary_protocol::parse_response(unsigned int latency)
{
    while (true) {
        int ret;
        int status;
        
        switch (m_response_state) {
            case rs_initial:
                m_last_response.clear();
                m_response_state = rs_multi_initial;
                m_response_len = 0;

            case rs_multi_initial:
                if (evbuffer_get_length(m_read_buf) < sizeof(m_response_hdr))
                    return 0;               // no header yet?

                ret = evbuffer_remove(m_read_buf, (void *)&m_response_hdr, sizeof(m_response_hdr));
                assert(ret == sizeof(m_response_hdr));

                if (m_response_hdr.message.header.response.magic != PROTOCOL_BINARY_RES) {
                    benchmark_error_log("error: invalid memcache response header magic.\n");
                    return -1;
                }

                m_response_len += sizeof(m_response_hdr);
                m_last_response.set_total_len(m_response_len);
                if (status_text()) {
                    m_last_response.set_status(strdup(status_text()));
                }

                status = ntohs(m_response_hdr.message.header.response.status);
                if (status == PROTOCOL_BINARY_RESPONSE_EINVAL ||
                    status == PROTOCOL_BINARY_RESPONSE_AUTH_ERROR ||
                    status == PROTOCOL_BINARY_RESPONSE_AUTH_CONTINUE ||
                    status == PROTOCOL_BINARY_RESPONSE_NOT_SUPPORTED ||
                    status == PROTOCOL_BINARY_RESPONSE_UNKNOWN_COMMAND ||
                    status == PROTOCOL_BINARY_RESPONSE_NOT_SUPPORTED ||
                    status == PROTOCOL_BINARY_RESPONSE_EBUSY) {
                    m_last_response.set_error(true);
                }

                m_last_response.set_latency(latency);

                if (ntohl(m_response_hdr.message.header.response.bodylen) > 0) {
                    m_response_hdr.message.header.response.bodylen = ntohl(m_response_hdr.message.header.response.bodylen);
                    m_response_hdr.message.header.response.keylen = ntohs(m_response_hdr.message.header.response.keylen);
                    
                    m_response_state = rs_read_body;
                    continue;
                }

                return 1;                

            case rs_read_body:
                if (evbuffer_get_length(m_read_buf) >= m_response_hdr.message.header.response.bodylen) {
                    // get rid of extras , we don't care about them
                    ret = evbuffer_drain(m_read_buf, 
                        m_response_hdr.message.header.response.extlen);
                    assert((unsigned int) ret == 0);

                    int actual_body_len = m_response_hdr.message.header.response.bodylen -
                        m_response_hdr.message.header.response.extlen;

                    if (m_keep_value) {
                        uint16_t keylen = m_response_hdr.message.header.response.keylen;
                        uint8_t opcode = m_response_hdr.message.header.response.opcode;
                        char* key = NULL;
                        actual_body_len = actual_body_len - keylen;
                        if (opcode == PROTOCOL_BINARY_CMD_GETK || opcode == PROTOCOL_BINARY_CMD_GETKQ) {
                            key = (char *) malloc(keylen);
                            assert(key != NULL);
                            ret = evbuffer_remove(m_read_buf, key, keylen);
                        } else {
                            evbuffer_drain(m_read_buf, keylen);
                        }
                        char *value = (char *) malloc(actual_body_len);
                        assert(value != NULL);
                        ret = evbuffer_remove(m_read_buf, value, actual_body_len);
                        m_last_response.set_value(value, actual_body_len, key, keylen);
                    } else {
                        int ret = evbuffer_drain(m_read_buf, actual_body_len);
                        assert((unsigned int) ret == 0);
                    }

                    if (m_response_hdr.message.header.response.status == PROTOCOL_BINARY_RESPONSE_SUCCESS)
                        m_last_response.incr_hits();
                    
                    m_response_len += m_response_hdr.message.header.response.bodylen;
                    m_last_response.set_total_len(m_response_len);
                    m_response_state = rs_initial;
                    if (m_response_hdr.message.header.response.opcode == PROTOCOL_BINARY_CMD_GETKQ) {
                        m_response_state = rs_multi_initial;
                        continue;
                    }
                    m_response_state = rs_initial;
                    return 1;
                }
                return 0;

            default:
                benchmark_debug_log("unknown response state.\n");
                return -1;
        }
    }

    return -1;
}

/////////////////////////////////////////////////////////////////////////

class abstract_protocol *protocol_factory(const char *proto_name)
{
    assert(proto_name != NULL);

    if (strcmp(proto_name, "redis") == 0) {
        return new redis_protocol();
    } else if (strcmp(proto_name, "memcache_text") == 0) {
        return new memcache_text_protocol();
    } else if (strcmp(proto_name, "memcache_binary") == 0) {
        return new memcache_binary_protocol();
    } else {
        benchmark_error_log("Error: unknown protocol '%s'.\n", proto_name);
        return NULL;
    }
}

/////////////////////////////////////////////////////////////////////////

keylist::keylist(unsigned int max_keys) :
    m_buffer(NULL), m_buffer_ptr(NULL), m_buffer_size(0),
    m_keys(NULL), m_keys_size(0), m_keys_count(0)
{
    init(max_keys);
}

keylist::keylist(const keylist &source) :
    m_buffer(NULL), m_buffer_ptr(NULL), m_buffer_size(0),
    m_keys(NULL), m_keys_size(0), m_keys_count(0)
{
    init(source.get_keys_count());
    for (unsigned int i = 0; i<source.get_keys_count(); i++) {
        unsigned int key_length = 0;
        const char* key;
        key = source.get_key(i, &key_length);
        add_key(key, key_length);
    }
}

keylist::~keylist()
{
    if (m_buffer != NULL) {
        free(m_buffer);
        m_buffer = NULL;
    }
    if (m_keys != NULL) {
        free(m_keys);
        m_keys = NULL;
    }
}

void keylist::init(unsigned int max_keys)
{
    m_keys_size = max_keys;
    m_keys = (key_entry *) malloc(m_keys_size * sizeof(key_entry));
    assert(m_keys != NULL);
    memset(m_keys, 0, m_keys_size * sizeof(key_entry));

    /* allocate buffer for actual keys */
    m_buffer_size = 256 * m_keys_size;
    m_buffer = (char *) malloc(m_buffer_size);
    assert(m_buffer != NULL);
    memset(m_buffer, 0, m_buffer_size);

    m_buffer_ptr = m_buffer;
}

bool keylist::add_key(const char *key, unsigned int key_len)
{
    // have room?
    if (m_keys_count >= m_keys_size)
        return false;

    // have buffer?
    if (m_buffer_ptr + key_len >= m_buffer + m_buffer_size) {
        while (m_buffer_ptr + key_len >= m_buffer + m_buffer_size) {
            m_buffer_size *= 2;
        }
        m_buffer = (char *)realloc(m_buffer, m_buffer_size);
        assert(m_buffer != NULL);
    }

    // copy key
    memcpy(m_buffer_ptr, key, key_len);
    m_buffer_ptr[key_len] = '\0';
    m_keys[m_keys_count].key_ptr = m_buffer_ptr;
    m_keys[m_keys_count].key_len = key_len;

    m_buffer_ptr += key_len + 1;
    m_keys_count++;

    return true;
}

unsigned int keylist::get_keys_count(void) const
{
    return m_keys_count;
}

const char *keylist::get_key(unsigned int index, unsigned int *key_len) const
{
    if (index < 0 || index >= m_keys_count)
        return NULL;
    if (key_len != NULL)
        *key_len = m_keys[index].key_len;
    return m_keys[index].key_ptr;
}

void keylist::clear(void)
{
    m_keys_count = 0;
    m_buffer_ptr = m_buffer;
}

