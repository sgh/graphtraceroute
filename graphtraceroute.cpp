#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <string>
#include <vector>
#include <map>
#include <iostream>

struct TraceNode;
struct TraceNode {
	TraceNode() {
		kbs = 0;
		root = false;
	}
	int kbs;
	std::string ip_address;
	std::string hostname;
	std::string label;
	bool root;
	std::vector<struct TraceNode*> children;
};

FILE* consolefp;

std::map<std::string,struct TraceNode*> all_connections;

const std::string resolve_address(const std::string& address) {
	int res;
	struct sockaddr_in sa;
	char hbuf[NI_MAXHOST];

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = inet_addr(address.c_str());

	if ( (res = getnameinfo((struct sockaddr*)&sa, sizeof(sa), hbuf, sizeof(hbuf), NULL, 0, NI_NAMEREQD)) == 0)
		return hbuf;

	return "";
}

struct TraceNode* ___create_node(const std::string& ip_address, int kbs) {
	struct TraceNode* ptr = new struct TraceNode;
	ptr->ip_address = ip_address;
	ptr->kbs = kbs;	
	return ptr;
}

void add_trace(std::vector<std::string>& trace, const std::string& leaflabel, int kbs) {
	struct TraceNode* parent = NULL;
	bool root = true;

	while (trace.size() > 0) {
		struct TraceNode* current;

		// Find host in map
		std::map<std::string,struct TraceNode*>::iterator connections_it = all_connections.find(*trace.begin());

		if (connections_it == all_connections.end()) {
			// Create host if it does not exist
			current = ___create_node(*trace.begin(), kbs);
			all_connections[*trace.begin()] = current;
		} else
			current = (*connections_it).second;
		trace.erase(trace.begin());

		// First node of each seperate trace is always a root
		if (root) {
			current->root = true;
			root = false;
		}

		if (current->kbs < kbs)
			current->kbs = kbs;

		// If this is not the first node then fill the parent with a pointer to this node.
		if (parent) {
			std::vector<struct TraceNode*>::iterator it = parent->children.begin();
			while (it != parent->children.end()) {
				if (current->ip_address == (*it)->ip_address)
					break;
				it++;
			}
			if (it == parent->children.end())
				parent->children.push_back(current);
		}

		if (trace.size() == 0)
			current->label = leaflabel;
		parent = current;
	}
}

std::string pretty_print(const struct TraceNode* node) {
	std::string pretty;

	if (node->hostname.length())
		pretty += node->hostname + "\\n";

	pretty += node->ip_address;

	if (node->label.length())
		pretty += "\\n" + node->label;

	return pretty;
}

void* resolver_thread(void* arg) {
	struct TraceNode* node = (struct TraceNode*)arg;
	node->hostname = resolve_address(node->ip_address);
	return NULL;
}

void resolve_ips(std::map<std::string,struct TraceNode*> node_map) {
	int res;
	pthread_attr_t attr;
	std::vector<pthread_t> v_threads;
	std::vector<pthread_t>::iterator threads_it;
	std::map<std::string, struct TraceNode*>::iterator map_it = node_map.begin();

	res = pthread_attr_init(&attr);
	if (res != 0)
		std::cerr << "Error initializing pthread_attr" << std::endl;

	fprintf(consolefp, "Resolving %d ip addresses to names ...", node_map.size());
	fflush(consolefp);
	while (map_it != node_map.end()) {
		pthread_t tmp_pthread;
		struct TraceNode* node = (*map_it).second;
		res = pthread_create(&tmp_pthread , &attr, &resolver_thread, node);
		if (res != 0)
			std::cerr << "Error starting thread" << std::endl;
		else
			v_threads.push_back(tmp_pthread);
		map_it++;
	}

	threads_it = v_threads.begin();
	while (threads_it != v_threads.end()) {
		res = pthread_join(*threads_it, NULL);
		if (res != 0)
			std::cerr << "Error joining thread" << std::endl;
		threads_it++;
	}
	fprintf(consolefp, "\n");
}

void fprintf_nodes(FILE* fp, std::map<std::string,struct TraceNode*> node_map) {
	const int kbs_win  = 700;
	const int kbs_fail = 400;
	unsigned int i;
	std::map<std::string, struct TraceNode*>::iterator it = node_map.begin();

	while (it != node_map.end()) {
		struct TraceNode* node = (*it).second;

		for (i=0; i<node->children.size(); i++) {
			unsigned char R = 0;
			unsigned char G = 0;
			unsigned char B = 0;
			int kbs = node->children[i]->kbs;
			if (node->children[i]->children.size() == 0) {
				fprintf(fp,"\"%s\" [shape=box];\n", pretty_print(node->children[i]).c_str());
			}

			if (kbs >= kbs_win)
				G = 0xff;
			else if (kbs >= kbs_fail) {
				unsigned int val = ((kbs - kbs_fail) * 512) / (kbs_win - kbs_fail);
				if (val > 255) {
					G = 255;
					R = 255 - (val - 255);
				} else {
					R = 255;
					G = val;
				}

// 				fprintf(stderr, "VAL: %d => %d (R:%d G:%d\n", kbs, val, R, G);

			} else
				R = 0xff;

			if (kbs == -1)
				fprintf(fp,"\nedge [label=\"\", color=\"#000000\", penwidth=5];\n")	;
			else
				fprintf(fp,"\nedge [label=\"%d KB/s\", color=\"#%02X%02X%02X\", penwidth=5];\n", kbs, R, G, B);

			fprintf(fp,"\"%s\" -> \"%s\";\n",pretty_print(node).c_str(), pretty_print(node->children[i]).c_str());
		}
		it++;
	}
}

void fprintf_leaf_nodes(FILE* fp, std::map<std::string,struct TraceNode*>& node_map) {
	std::map<std::string, struct TraceNode*>::iterator it = node_map.begin();

	while (it != node_map.end()) {
		struct TraceNode* node = (*it).second;
		if (node->children.size() == 0)
			fprintf(fp,"\"%s\";\n",pretty_print(node).c_str());
		it++;
	}
}

void fprintf_root_nodes(FILE* fp, std::map<std::string,struct TraceNode*>& node_map) {
	std::map<std::string, struct TraceNode*>::iterator it = node_map.begin();

	while (it != node_map.end()) {
		struct TraceNode* node = (*it).second;
		if (node->root)
			fprintf(fp,"\"%s\";\n",pretty_print(node).c_str());
		it++;
	}
}

void free_nodes(std::map<std::string,struct TraceNode*>& node_map) {
	std::map<std::string, struct TraceNode*>::iterator it = node_map.begin();

	while (it != node_map.end()) {
		delete (*it).second;
		it++;
	}
	node_map.clear();
}


char url2host(const char* url, char* host, int hostlen) {
	const char* ptr = strstr(url,"//");
	
	if (!ptr) {
		strcpy(host, url);
		return 1;
	}

	if (ptr) {
		ptr += 2; // Skip "//"
		const char* ptr_colon = strstr(ptr,":");
		const char* ptr_slash = strstr(ptr,"/");
		const char* ptr2 = NULL;

		if (!ptr2 || (ptr_slash && ptr_slash<ptr2))
			ptr2 = ptr_slash;

		if (!ptr2 || (ptr_colon && ptr_colon<ptr2))
			ptr2 = ptr_colon;

		if (ptr2) {
			memset(host, 0, hostlen);
			strncpy(host, ptr, ptr2-ptr);
			return 0;
		}
	}
	fprintf(stderr,"Unble to parse hostname in url: %s\n", url);
	return 1;
}

void tracehost(const std::string& host, int kbs) {
	std::vector<std::string> traceroute;
	std::vector<std::string>::iterator it;
	char buffer[1024];
	FILE* fp;
	int status;
	unsigned int max_hop = 0;

	strcpy(buffer, "mtr --raw -n -c 1 ");
	strcat(buffer, host.c_str());
	strcat(buffer, " 2> /dev/null");

	/* Spawn mtr */
	fprintf(consolefp, "Tracing %s   ", host.c_str());
	fp = popen(buffer, "r");
	if (fp == NULL) {
		fprintf(stderr, "popen error\n");
		exit(1);
	}

	unsigned int progress = 0;
	const char progress_str[] = {'-','\\','|','/'};
	while ( fgets(buffer, sizeof(buffer), fp) != NULL) {
		unsigned int hop;
		char cmd;
		char tmpbuf[1024];
// 		fprintf(stderr,"%s\n", buffer);

		if (sscanf(buffer, "%c %d %s", &cmd, &hop, tmpbuf) != 3)
			continue;

		if (cmd == 'p') {
			if (consolefp) fprintf(consolefp, "\b\b%c ",progress_str[progress]);
			fflush(stdout);
			progress ++;
			if (progress >= sizeof(progress_str))
				progress = 0;
		}

		if (hop > max_hop)
			max_hop = hop;

		if (cmd == 'h' || cmd == 'd') {
// 			fprintf(stderr, "Hop: %d\n", hop);
			if (traceroute.size() <= hop)
				traceroute.resize(hop+1);
			traceroute[hop] = tmpbuf;
// 			fprintf(stderr, "Setting hop %d %s\n", hop, traceroute[hop].c_str());
		}

		if (cmd == 'p' && hop == 0) {
			max_hop = 0;
		}
	}
	traceroute.resize(max_hop+1);
	if (consolefp) fprintf(consolefp, "\b\b  \n");

	status = pclose(fp);
	if (status == -1) {
		fprintf(stderr,"pclose error\n");
		exit(1);
	}

	// Remove duplicate and empty hosts
	for (it = traceroute.begin()+1; it != traceroute.end(); it++) {
		if ( *(it-1) == *it) {
			traceroute.erase(it);
			it = traceroute.begin();
		}
	}

// 	unsigned int i;
// 	for (i=0; i<traceroute.size(); i++)
// 		fprintf(stderr, "TRACE[%d]: %s\n", i, traceroute[i].c_str());

	add_trace(traceroute, host, kbs);
}

int getspeed(const char* url) {
	char buffer[1024];
	FILE* fp;
	int status;
	float kbs = -1;

	url2host(url, buffer, sizeof(buffer));
	if (consolefp) fprintf(consolefp, "Measuring speed from %s   ", buffer);

	strcpy(buffer, "wget --delete-after \"");
	strcat(buffer, url);
	strcat(buffer, "\" 2>&1");

	/* Spawn wget */
	fp = popen(buffer, "r");
	if (fp == NULL) {
		fprintf(stderr, "popen error\n");
		exit(1);
	}

	unsigned int progress = 0;
	const char progress_str[] = {'-','\\','|','/'};
	while ( fgets(buffer, sizeof(buffer), fp) != NULL) {
		/* Match the last statistics line */
		char MB = 0;
		char * end;
		end = strstr(buffer, "KB/s");
		if (consolefp) fprintf(consolefp, "\b\b%c ",progress_str[progress]);
		fflush(stdout);
		progress ++;
		if (progress >= sizeof(progress_str))
			progress = 0;
		if (!end) {
			end = strstr(buffer, "MB/s");
			MB = 1;
		}
		char * ptr = end;
		if (ptr) {
			/* Wind ptr back to the starting parathesis */
			while (ptr >= buffer && *ptr!='(') {
				if (*ptr == ',')
					*ptr = '.';
				ptr--;
			}
			int len = end-ptr-1;
			memcpy(buffer,ptr+1, len);
			buffer[len] = 0;
			kbs = atof(buffer);
			if (MB)
				kbs *= 1024;
		}
	}
	if (consolefp) fprintf(consolefp, "\b\b%d KB/s\n",(int)kbs);

	status = pclose(fp);

	if (status == -1) {
		fprintf(stderr,"pclose error\n");
		exit(1);
	}

	return kbs;
}


struct host_speed {
	char* hostname;
	int kbs;
};
struct host_speed* speed_history;
int history_length = 0;

void speed_history_readfile(FILE* logfp) {
	while (!feof(logfp)) {
		char buffer[1024];
		char tmp[1024];
		int len;
		int kbs = 0;
		if (fgets(buffer, sizeof(buffer), logfp) == NULL && !feof(logfp)) {
			fprintf(stderr, "Unable to read logfile\n");
			exit(1);
		}
		len = strlen(buffer);
		if (len > 0) {
			while (!isalnum(buffer[len-1])) {
				len --;
				buffer[len] = 0;
			}

			if (sscanf(buffer,"%s %d", tmp, &kbs) == 2) {
				
				if (history_length == 0)
					speed_history = (struct host_speed*) malloc(sizeof(struct host_speed));
				else
					speed_history = (host_speed*)realloc(speed_history, (history_length+1)*(sizeof(struct host_speed)));

				speed_history[history_length].hostname = strdup(tmp);
				speed_history[history_length].kbs = kbs;
				history_length++;
			}
		}
	}
}

const struct host_speed* speed_history_max_kbs(const char* hostname) {
	int idx;
	struct host_speed* sh = NULL;
	for (idx=0; idx<history_length; idx++) {
		if (strcmp(speed_history[idx].hostname, hostname) == 0) {
			if (!sh || sh->kbs < speed_history[idx].kbs)
				sh = &speed_history[idx];
		}
	}
	return sh;
}

int speed_history_avr_kbs(const char* hostname, int kbs) {
	int idx;
	char matches = 0;
	float val = 0;
	for (idx=0; idx<history_length; idx++) {
		if (strcmp(speed_history[idx].hostname, hostname) == 0) {
			val = matches ? val*0.9 + ((float)speed_history[idx].kbs)*0.1 : kbs;
			matches++;
		}
	}
	val = matches ? val*0.9 + kbs*0.1 : kbs;
	return val;
}

void print_usage() {
	struct usage_option {
		const char* opt;
		const char* description;
	} usage[] = {
		{"-f <file>", "Read URLS from <file>. (default: stdin)"},
		{"-l <file>", "Log speeds to <file>"},
		{"-o <file>", "Write DOT-output to <file>. (default: stdout)"},
		{"-q", "Be quiet"},
		{NULL, NULL},
	};
	printf("Usage: graphtraceroute [OPTIONS]...\n");

	int idx = 0;
	while (usage[idx].opt != NULL) {
		printf("%-13s %s\n", usage[idx].opt, usage[idx].description);
		idx++;
	}
}

int main(int argc, char* argv[]) {
	char host[1024];
	char url[1024];
	FILE* dotoutfp = stdout;
	FILE* logfp = NULL;
	FILE* urlsfp = stdin;
	consolefp =  stderr;
	
	int option_charater;
	while ((option_charater = getopt(argc, argv, "o:f:l:q")) != -1) {
// 		printf("%d %s\n", optind, optarg);
		switch (option_charater) {
			case 'o':
				if (!(dotoutfp = fopen(optarg, "w"))) {
					fprintf(stderr,"Unable to open outputfile\n");
					return 1;
				}
				if (consolefp)
					consolefp = stdout;
				break;
			case 'f':
				if (!(urlsfp = fopen(optarg,"a+"))) {
					fprintf(stderr,"Unable to open inputfile: %s\n", optarg);
					return 1;
				}
				break;
			case 'l':
				if (!(logfp = fopen(optarg,"a+"))) {
					fprintf(stderr,"Unable to open logfile %s\n", optarg);
					return 1;
				}
				speed_history_readfile(logfp);
				break;
			case 'q':
				consolefp = NULL;
				break;
			case '?':
				print_usage();
				exit(1);
				break;
		}

	}

	int idx = 0; 

	idx=0;
	while (fgets(url, sizeof(url), urlsfp) != NULL && !feof(urlsfp)) {
		char meassure_speed = 1;
		int len = strlen(url);
		int kbs;
		while (!isalnum(url[len-1])) {
			len --;
			url[len] = 0;
		}

		if (len > 0 && url[0] == '#')
			continue;

		if (url2host(url, host, sizeof(host)))
			meassure_speed = 0;

		kbs = -1;
		if (meassure_speed) {
			kbs = getspeed(url);
			if (logfp && kbs != -1)
				fprintf(logfp, "%s %d\n", host, kbs);

			kbs = speed_history_avr_kbs(host, kbs);
		}
		tracehost(host, kbs);
		idx++;
	}

	resolve_ips(all_connections);

	fprintf(dotoutfp,"digraph A  {\n");
	fprintf(dotoutfp,"node [fontsize=10];\n");
	fprintf(dotoutfp,"edge [fontsize=10, penwidth=3];\n");

	fprintf_nodes(dotoutfp, all_connections);

	fprintf(dotoutfp, "\n{ rank=same;\n");
	fprintf_leaf_nodes(dotoutfp, all_connections);
	fprintf(dotoutfp, "}\n");
	
	fprintf(dotoutfp, "\n{ rank=same;\n");
	fprintf_root_nodes(dotoutfp, all_connections);
	fprintf(dotoutfp, "}\n");

	free_nodes(all_connections);
	fprintf(dotoutfp,"}\n");

	if (logfp)              fclose(logfp);
	if (urlsfp != stdin)    fclose(urlsfp);
	if (dotoutfp != stdout) fclose(dotoutfp);

	return 0;
}
