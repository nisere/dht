#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libmemcached/memcached.h>
#include <openssl/sha.h>
#include <openssl/md5.h>
#ifdef TEST_THROUGHPUT
#include <sys/time.h>
#endif

memcached_return_t error;
int err_code;

typedef struct _server {
	unsigned long int hash;
	memcached_st *memc;
} server;

int no_servers = 0;
server **servers = NULL;

int create_servers()
{
	FILE *stream;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	size_t i;
	char ip[33];

	unsigned long int maximum_hash = 0xFFFFFFFF;
	unsigned long int delta_hash = maximum_hash / no_servers;

	/* Read the file containing the ip of servers */
	stream = fopen("servers", "r");
	if (stream == NULL) {
		return -1;
	}
		
	/* Allocate room for an array of pointers to server structure */
	servers = (server **)calloc(no_servers, sizeof(server *));
	if (servers == NULL) {
		fclose(stream);
		return -2;
	}

	for (i = 0; i < no_servers; i++) {
		if( (read = getline(&line, &len, stream)) == -1 ) {
			break;
		}

		/* Allocate space for current server */
		server *s = NULL;
		s = (server *)malloc(sizeof(server));
		if (s == NULL) {
			free(line);
			fclose(stream);
			return -3;
		}

		strncpy(ip, line, 32);
		ip[32] = '\0';

		/* Create the memcached sever associated with current server */
		s->memc = memcached_create(NULL);
		if (s->memc == NULL) {
			free(line);
			fclose(stream);
			return -4;
		}
		error = memcached_server_add(s->memc, ip, 0);
		if (error != MEMCACHED_SUCCESS) {
			free(line);
			fclose(stream);
			return -5;
		}

		/* Determine the hash associated with current server */
		if (i == no_servers - 1) {
			s->hash = maximum_hash;
		} else {
			s->hash = (i + 1)*delta_hash;
		}

		/* Attach current server to the array of pointer to servers */
		servers[i] = s;
	}

	if (line != NULL) {
		free(line);
	}
	fclose(stream);

	/* Check if less than no_servers servers were read */
	if (i < no_servers) {
		return -6;
	}

	return 0;
}

unsigned long int md5_hash(char *key, int keylen)
{
	unsigned long int hash = 0lu;
	unsigned char temp[MD5_DIGEST_LENGTH];

	memset(temp, 0x0, MD5_DIGEST_LENGTH);

	MD5((unsigned char *)key, keylen, temp);

	hash  = ((unsigned long int) temp[0]) << 24;
	hash |= ((unsigned long int) temp[1]) << 16;
	hash |= ((unsigned long int) temp[2]) << 8;
	hash |= ((unsigned long int) temp[3]);

	return hash;
}

unsigned long int sha_hash(char *key, int keylen)
{
	unsigned long int hash = 0lu;
	unsigned char temp[SHA_DIGEST_LENGTH];

	memset(temp, 0x0, SHA_DIGEST_LENGTH);

	SHA1((unsigned char *)key, keylen, temp);

	hash  = ((unsigned long int) temp[0]) << 24;
	hash |= ((unsigned long int) temp[1]) << 16;
	hash |= ((unsigned long int) temp[2]) << 8;
	hash |= ((unsigned long int) temp[3]);

	return hash;
}

int get_server_index(unsigned long int hash)
{
	int index = 0;
	int first = 0;
	int last = no_servers - 1;
	int middle = (first + last)/2;
	
	server *s;

	while (first <= last) {
		s = servers[middle];
		if (s->hash < hash) {
			first = middle + 1;    
		} else if (s->hash == hash) {
			index = middle + 1;
			break;
		} else {
			last = middle - 1;
		}

		middle = (first + last)/2;
	}
	if (first > last) {
		index = first;
	}

	return index;
}

int successor(int index)
{
	return (index < no_servers - 1) ? (index + 1) : 0;
}

int put(char *key, int keylen, char *value, size_t valuelen)
{
	int ret1 = 0;
	int ret2 = 0;
	unsigned long int hash1, hash2;
	int index1, index2;
	server *s;

	/* Calculate key hash */
	hash1 = sha_hash(key, keylen);
	hash2 = md5_hash(key, keylen);

	/* Determine the servers that will receive the data */
	index1 = get_server_index(hash1);
	index2 = get_server_index(hash2);

	/* If we have collision use the next server as the 2nd server */
	if (index1 == index2) {
		index2 = successor(index2);
	}

	//printf("put on servers %d and %d\n", index1, index2);

#ifdef TEST_DISTRIBUTION
	printf("%d\n%d\n", index1, index2);
	return 0;
#endif

	/* Try put on the first replica.
	 * If it fails, try the successor only if it's different from the second replica
	 */
	s = servers[index1];
	error = memcached_set(s->memc, key, keylen, value, valuelen, 0, 0);
	//printf("Error %d: %s\n", index1, memcached_strerror(s->memc, error));
	if ( (error != MEMCACHED_SUCCESS) && (successor(index1) != index2) ) {
		s = servers[successor(index1)];
		error = memcached_set(s->memc, key, keylen, value, valuelen, 0, 0);
		//printf("Error %d: %s\n", successor(index1), memcached_strerror(s->memc, error));
		if (error != MEMCACHED_SUCCESS) {
			ret1 = 1;
		}
	}

	/* Try put on the second replica.
	 * If it fails, try the successor only if it's different from the first replica
	 */
	s = servers[index2];
	error = memcached_set(s->memc, key, keylen, value, valuelen, 0, 0);
	//printf("Error %d: %s\n", index2, memcached_strerror(s->memc, error));
	if ( (error != MEMCACHED_SUCCESS) && (successor(index2) != index1) ) {
		s = servers[successor(index2)];
		error = memcached_set(s->memc, key, keylen, value, valuelen, 0, 0);
		//printf("Error %d: %s\n", successor(index2), memcached_strerror(s->memc, error));
		if (error != MEMCACHED_SUCCESS) {
			ret2 = 1;
		}
	}
		
	return -(ret1 & ret2);
}

int get(char *key, int keylen, char **value, size_t *valuelen)
{
	unsigned long int hash1, hash2;
	int index1, index2;
	uint32_t flags = 0;
	server *s;

	/* Calculate key hash */
	hash1 = sha_hash(key, keylen);
	hash2 = md5_hash(key, keylen);
	
	/* Determine the servers that has the data */
	index1 = get_server_index(hash1);
	index2 = get_server_index(hash2);

	/* If we have collision use the next server as the 2nd server */
	if (index1 == index2) {
		index2 = successor(index2);
	}
	
	//printf("get from servers %d or %d\n", index1, index2);
	
	/* Try get from the first replica.
	 * If it fails, try the second replica.
	 * If it fails, try the successor of the first replica only if it's different from the second replica.
	 * If it fails, try the successor of the second replica only if it's different from the first replica.
	 */

	s = servers[index1];
	*value = memcached_get(s->memc, key, keylen, valuelen, &flags, &error);
	//printf("Error %d: %s\n", index1, memcached_strerror(s->memc, error));
	if (error == MEMCACHED_SUCCESS) {
		return 0;
	}	

	s = servers[index2];
	*value = memcached_get(s->memc, key, keylen, valuelen, &flags, &error);
	//printf("Error %d: %s\n", index2, memcached_strerror(s->memc, error));
	if (error == MEMCACHED_SUCCESS) {
		return 0;
	} 

	if (successor(index1) != index2) {
		s = servers[successor(index1)];
		*value = memcached_get(s->memc, key, keylen, valuelen, &flags, &error);
		//printf("Error %d: %s\n", successor(index1), memcached_strerror(s->memc, error));
		if (error == MEMCACHED_SUCCESS) {
			return 0;
		}
	}

	if (successor(index2) != index1) {
		s = servers[successor(index2)];
		*value = memcached_get(s->memc, key, keylen, valuelen, &flags, &error);
		//printf("Error %d: %s\n", successor(index2), memcached_strerror(s->memc, error));
		if (error == MEMCACHED_SUCCESS) {
			return 0;
		}
	}

	return -1;
}

void clean_up()
{
	int i;
	server *s;
	
	if (servers == NULL) {
		return;
	}
	
	for (i = 0; i < no_servers; i++) {
		s = servers[i];
		//printf("%lu\n", s->hash);
		if (s == NULL) {
			return;
		}
		memcached_free(s->memc);
		free(s);
	}
	
	free(servers);
}

#ifdef TEST_DISTRIBUTION
void test_distribution(int no_keys)
{
	int i;
	char key[10];

	for (i = 0; i < no_keys; i++) {
		sprintf(key, "key_%06d", i);
		put(key, 10, NULL, 0);
	}
}
#endif

#ifdef TEST_THROUGHPUT
float test_throughput_write(int no_keys, int valuelen)
{
	int i;
	char key[10];
	char *value = malloc(valuelen);

	struct timeval tv;
	struct timezone tz;
	time_t starts;
	suseconds_t startu;

	gettimeofday(&tv, &tz);
	starts = tv.tv_sec;
	startu = tv.tv_usec;

	for (i = 0; i < no_keys; i++) {
		sprintf(key, "key_%06d", i);
		put(key, 10, value, valuelen);
	}

	gettimeofday(&tv, &tz);

	free(value);

	return (tv.tv_sec - starts) + (tv.tv_usec - startu) / 1000000.0f;
}

float test_throughput_read(int no_keys)
{
	int i;
	char key[10];
	char *value;
	size_t valuelen;

	struct timeval tv;
	struct timezone tz;
	time_t starts;
	suseconds_t startu;

	gettimeofday(&tv, &tz);
	starts = tv.tv_sec;
	startu = tv.tv_usec;

	for (i = 0; i < no_keys; i++) {
		sprintf(key, "key_%06d", i);
		get(key, 10, &value, &valuelen);
	}

	gettimeofday(&tv, &tz);

	free(value);

	return (tv.tv_sec - starts) + (tv.tv_usec - startu) / 1000000.0f;
}
#endif

#ifdef TEST_AVAILABILITY
int test_availability_write(int no_keys)
{
	int ret = 0;
	int i;
	int err;
	char key[10];
	char *value = malloc(1);

	for (i = 0; i < no_keys; i++) {
		sprintf(key, "key_%06d", i);
		if ( (err = put(key, 10, value, 1)) == 0) {
			ret++;
		}
	}

	free(value);

	return ret;
}

int test_availability_read(int no_keys)
{
	int ret = 0;
	int i;
	int err;
	char key[10];
	char *value;
	size_t valuelen;

	for (i = 0; i < no_keys; i++) {
		sprintf(key, "key_%06d", i);
		if ( (err = get(key, 10, &value, &valuelen)) == 0) {
			ret++;
		}
	}

	free(value);

	return ret;
}
#endif

void read_input()
{
	char in_cmd[4];
	char in_key[11];
	char in_value[256];
	char *line = NULL;
	size_t len = 0;
	int read;
	char *tok;

	printf("Usage:\nput [key] [value]\nget [key]\nexit\n");

	while ( (read = getline(&line, &len, stdin)) != -1 ) {
		in_cmd[0] = '\0';
		in_key[0] = '\0';
		in_value[0] = '\0';

		tok = strtok(line, "\n ");
		if (tok != NULL) {
			//printf("%s ", tok);
			strncpy(in_cmd, tok, 3);
			in_cmd[3] = '\0';
			tok = strtok(NULL, "\n ");
		}
		if (tok != NULL) {
			//printf("%s ", tok);
			int keylen = (strlen(tok) < 10) ? strlen(tok) : 10;
			strncpy(in_key, tok, keylen);
			in_key[keylen] = '\0';
			tok = strtok(NULL, "\n");
		}
		if (tok != NULL) {
			//printf("%s ", tok);
			int valuelen = (strlen(tok) < 255) ? strlen(tok) : 255;
			strncpy(in_value, tok, valuelen);
			in_value[valuelen] = '\0';
		}

		//printf("cmd: %s(%d) %s(%d) %s(%d)\n", in_cmd, strlen(in_cmd), in_key, strlen(in_key), in_value, strlen(in_value));
		if ( (strcmp(in_cmd, "put") == 0) && (strlen(in_key) !=0) && (strlen(in_value) != 0) ) {
			put(in_key, strlen(in_key), in_value, strlen(in_value));
		} else if ( (strcmp(in_cmd, "get") == 0) && (strlen(in_key) !=0) ) {
			char *value = NULL;
			size_t valuelen = 0;

			get(in_key, strlen(in_key), &value, &valuelen);
			if (value == NULL) {
				printf("Object not found!\n");
			} else {
				printf("%s\n", value);
			}
		} else if ( strcmp(in_cmd, "exi") == 0 ) {
			break;
		}

	}

	free(line);
}

int main(int argc, char **argv)
{
#ifdef TEST_THROUGHPUT
	if (argc < 4) {
		printf("Usage: ./dht [number of servers] [number of keys] [object size]\n");
#elif defined(TEST_DISTRIBUTION) || defined(TEST_AVAILABILITY)
	if (argc < 3) {
		printf("Usage: ./dht [number of servers] [number of keys]\n");
#else
	if (argc == 1) {
		printf("Usage: ./dht [number of servers]\n");
#endif
		exit(EXIT_FAILURE);
	}

	no_servers = atoi(argv[1]);

	if (no_servers <= 0) {
		printf("Invalid number of servers\n");
		exit(EXIT_FAILURE);
	}

	if ( (err_code = create_servers()) ) {
		printf("Error: %d\n", err_code);
		clean_up();
		exit(EXIT_FAILURE);
	}

#if defined(TEST_DISTRIBUTION) || defined(TEST_THROUGHPUT) || defined(TEST_AVAILABILITY)
	int no_keys;
	no_keys = atoi(argv[2]);
	if (no_keys <= 0) {
		printf("Invalid number of keys\n");
		exit(EXIT_FAILURE);
	}
#endif

#if defined(TEST_DISTRIBUTION)
	test_distribution(no_keys);
#elif defined(TEST_THROUGHPUT)
	int valuelen;

	valuelen = atoi(argv[3]);
	if (valuelen <= 0) {
		printf("Invalid object size\n");
		exit(EXIT_FAILURE);
	}

	printf("%d %d %f ", no_keys, valuelen, test_throughput_write(no_keys, valuelen));
	printf("%f\n", test_throughput_read(no_keys));
#elif defined(TEST_AVAILABILITY)
	int wr, re;

	wr = test_availability_write(no_keys);
	printf("Deactivate/Activate links and press enter... ");
	getchar();
	re = test_availability_read(no_keys);
	printf("%d %d %d\n", no_keys, wr, re);
#else
	read_input();
#endif

	clean_up();

	exit(EXIT_SUCCESS);
}
