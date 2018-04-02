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

#ifndef _OBJ_GEN_H
#define _OBJ_GEN_H

#include <vector>
#include <stdint.h>
#include "file_io.h"

struct random_data;
struct config_weight_list;

class random_generator {
public:
    random_generator();
    unsigned long long get_random();
    unsigned long long get_random_max() const;
    void set_seed(int seed);
private:
#ifdef HAVE_RANDOM_R
    struct random_data m_data_blob;
    char m_state_array[512];
#elif (defined HAVE_DRAND48)
    unsigned short m_data_blob[3];
#endif
};

class gaussian_noise: public random_generator {
public:
    gaussian_noise() { m_hasSpare = false; }
    unsigned long long gaussian_distribution_range(double stddev, double median, unsigned long long min, unsigned long long max);
private:
    double gaussian_distribution(const double &stddev);
    bool m_hasSpare;
	double m_spare;
};

class data_object {
protected:    
    const char *m_key;
    unsigned int m_key_len;
    const char *m_value;
    unsigned int m_value_len;
    unsigned int m_expiry;
public:
    data_object();
    ~data_object();

    void clear(void);
    void set_key(const char* key, unsigned int key_len);
    const char* get_key(unsigned int* key_len);
    void set_value(const char* value, unsigned int value_len);
    const char* get_value(unsigned int* value_len);
    void set_expiry(unsigned int expiry);
    unsigned int get_expiry(void);    
};

class crc32 {
public:
    static const unsigned int size = 4;
    static uint32_t calc_crc32(const void *buffer, unsigned long length, const void *key, unsigned int key_length);
private:
    static const unsigned int crctab[256];
};

#define OBJECT_GENERATOR_KEY_ITERATORS  2 /* number of iterators */
#define OBJECT_GENERATOR_KEY_SET_ITER   1
#define OBJECT_GENERATOR_KEY_GET_ITER   0
#define OBJECT_GENERATOR_KEY_RANDOM    -1
#define OBJECT_GENERATOR_KEY_GAUSSIAN  -2

class object_generator {
public:
    enum data_size_type { data_size_unknown, data_size_fixed, data_size_range, data_size_weighted };
protected:
    data_size_type m_data_size_type;
    union {
        unsigned int size_fixed;
        struct {
            unsigned int size_min;
            unsigned int size_max;
        } size_range;
        config_weight_list* size_list;
    } m_data_size;
    const char *m_data_size_pattern;
    bool m_random_data;
    float m_compression_ratio;
    unsigned int m_expiry_min;
    unsigned int m_expiry_max;
    const char *m_key_prefix;
    unsigned long long m_key_min;
    unsigned long long m_key_max;
    double m_key_stddev;
    double m_key_median;
    data_object m_object;

    unsigned long long m_next_key[OBJECT_GENERATOR_KEY_ITERATORS];

    unsigned long long m_key_index;
    char m_key_buffer[250];
    char *m_value_buffer;
    int m_random_fd;
    gaussian_noise m_random;
    unsigned int m_value_buffer_size;
    unsigned int m_value_buffer_random_part_size;
    unsigned int m_value_buffer_mutation_pos;
    
    virtual void alloc_value_buffer(void);
    virtual void alloc_value_buffer(const char* copy_from);
    void random_init(void);
    unsigned long long get_key_index(int iter);
public:    
    object_generator();
    object_generator(const object_generator& copy);
    virtual ~object_generator();
    virtual object_generator* clone(void);

    unsigned long long random_range(unsigned long long r_min, unsigned long long r_max);
    unsigned long long normal_distribution(unsigned long long r_min, unsigned long long r_max, double r_stddev, double r_median);

    void set_random_data(bool random_data);
    void set_compression_ratio(float compression_ratio);
    void set_data_size_fixed(unsigned int size);
    void set_data_size_range(unsigned int size_min, unsigned int size_max);
    void set_data_size_list(config_weight_list* data_size_list);
    void set_data_size_pattern(const char* pattern);
    void set_expiry_range(unsigned int expiry_min, unsigned int expiry_max);
    void set_key_prefix(const char *key_prefix);    
    void set_key_range(unsigned long long key_min, unsigned long long key_max);
    void set_key_distribution(double key_stddev, double key_median);
    void set_random_seed(int seed);

    virtual const char* get_key(int iter, unsigned int *len);
    virtual data_object* get_object(int iter);
};

class imported_keylist;
class memcache_item;

class imported_keylist {
protected:
    struct key {
        unsigned int key_len;
        char key_data[0];
    };
    const char *m_filename;
    std::vector<key*> m_keys;
public:
    imported_keylist(const char *filename);
    ~imported_keylist();

    bool read_keys(void);
    unsigned int size();
    const char* get(unsigned int pos, unsigned int *len);
};

class import_object_generator : public object_generator {
protected:
    imported_keylist* m_keys;
    file_reader m_reader;
    memcache_item* m_cur_item;
    bool m_reader_opened;
    bool m_no_expiry;
public:
    import_object_generator(const char *filename, imported_keylist* keys, bool no_expiry);
    import_object_generator(const import_object_generator& from);
    virtual ~import_object_generator();
    virtual import_object_generator* clone(void);

    virtual const char* get_key(int iter, unsigned int *len);
    virtual data_object* get_object(int iter);

    bool open_file(void);
};

class crc_object_generator : public object_generator {
protected:
    unsigned int m_crc_size;
    unsigned int m_actual_value_size;
    char *m_crc_buffer;

    virtual void alloc_value_buffer(void);
    virtual void alloc_value_buffer(const char* copy_from);
public:
    explicit crc_object_generator();
    crc_object_generator(const crc_object_generator& from);
    virtual ~crc_object_generator() {}
    virtual crc_object_generator* clone(void);

    virtual data_object* get_object(int iter);
    unsigned int get_actual_value_size();
    void reset_next_key();
};

#endif /* _OBJ_GEN_H */
