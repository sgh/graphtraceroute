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

#include <string>
#include <vector>

struct TraceNode;
struct TraceNode {
	TraceNode() {
		kbs = 0;
	}
	int kbs;
	std::string hostname;
	std::vector<struct TraceNode*> children;
};

FILE* consolefp;

struct TraceNode* all_routes;

struct TraceNode* ___create_node(const std::string& hostname, int kbs) {
	struct TraceNode* ptr = new struct TraceNode;
	ptr->hostname = hostname;
	ptr->kbs = kbs;	
	return ptr;
}

void ___add_trace(struct TraceNode* node, std::vector<std::string>& trace, int kbs) {
	// Return of to more hops are available
	if (trace.size() == 0)
		return;

	if (node->kbs < kbs)
		node->kbs = kbs;

	// If current node matches
	if (node->hostname == *trace.begin()) { // Hostname-match traverse to child nodes
		trace.erase(trace.begin());
		// Search children for match
		for (unsigned i=0; i<node->children.size(); i++) {
			if (node->children[i]->hostname == *trace.begin()) {
				___add_trace(node->children[i], trace, kbs);
				return;
			}
		}
	}

	if (trace.size() == 0)
		return;

	// If fist hostname is empty ignore it and move to the next
	if ((*trace.begin()).length() == 0) {
		trace.erase(trace.begin());
		___add_trace(node, trace, kbs);
		return;
	}

	node->children.push_back(___create_node((*trace.begin()).c_str(), kbs));
	___add_trace(node->children.back(), trace, kbs);
}


void add_trace(std::vector<std::string>& trace, int kbs) {
	if (!all_routes) { // Create initial localhost node
		all_routes = ___create_node(*trace.begin(), kbs);
		trace.erase(trace.begin());
	}

// 	fprintf(stderr, "Traversing nodes \n");
	___add_trace(all_routes, trace, kbs);
}

void fprintf_nodes(FILE* fp, struct TraceNode* node) {
	const int kbs_win  = 700;
	const int kbs_fail = 400;
	unsigned int i;

	if (!node)
		return;

	for (i=0; i<node->children.size(); i++) {
		unsigned char R = 0;
		unsigned char G = 0;
		unsigned char B = 0;
		int kbs = node->children[i]->kbs;
		if (node->children[i]->children.size() == 0) {
			fprintf(fp,"\"%s\" [shape=box];\n", node->children[i]->hostname.c_str());
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
			
// 			fprintf(stderr, "VAL: %d => %d (R:%d G:%d\n", kbs, val, R, G);
			
		} else
			R = 0xff;

		if (kbs == -1)
			fprintf(fp,"\nedge [label=\"\", color=\"#000000\", penwidth=5];\n");
		else
			fprintf(fp,"\nedge [label=\"%d KB/s\", color=\"#%02X%02X%02X\", penwidth=5];\n", kbs, R, G, B);

		fprintf(fp,"\"%s\" -> \"%s\";\n", node->hostname.c_str(), node->children[i]->hostname.c_str());
	}

	for (i=0; i<node->children.size(); i++)
		fprintf_nodes(fp, node->children[i]);
}

void fprintf_leaf_nodes(FILE* fp, struct TraceNode* node) {
	unsigned int i;

	if (!node)
		return;

	if (node->children.size() != 0) {
		for (i=0; i<node->children.size(); i++)
			fprintf_leaf_nodes(fp, node->children[i]);
	} else {
		fprintf(fp,"\"%s\";\n",node->hostname.c_str());
	}
}


void free_nodes(struct TraceNode* node) {
	unsigned int i;

	if (!node)
		return;
	for (i=0; i<node->children.size(); i++)
		free_nodes(node->children[i]);

	node->children.clear();
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
	char buffer[1024];
	FILE* fp;
	int status;
	unsigned int max_hop = 0;

	strcpy(buffer, "mtr --raw -c 5 ");
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

		if (consolefp) fprintf(consolefp, "\b\b%c ",progress_str[progress]);
		fflush(stdout);
		progress ++;
		if (progress >= sizeof(progress_str))
			progress = 0;

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
// 			fprintf(stderr, "Resizing\n");
			traceroute.resize(max_hop+1);
			max_hop = 0;
		}
	}
	if (consolefp) fprintf(consolefp, "\n");

	status = pclose(fp);
	if (status == -1) {
		fprintf(stderr,"pclose error\n");
		exit(1);
	}

	// Add url host at the end
// 	fprintf(stderr, "¤¤¤¤ %s %s\n", traceroute[max_hop-1], host);
	if (traceroute[max_hop] != host)
		traceroute[max_hop] += "\\n[" + host + "]";

// 	unsigned int i;
// 	for (i=0; i<traceroute.size(); i++)
// 		fprintf(stderr, "TRACE[%d]: %s\n", i, traceroute[i].c_str());

	add_trace(traceroute, kbs);
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

	fprintf(dotoutfp,"digraph A  {\n");
	fprintf(dotoutfp,"node [fontsize=10];\n");
	fprintf(dotoutfp,"edge [fontsize=10, penwidth=3];\n");

	fprintf_nodes(dotoutfp, all_routes);

	fprintf(dotoutfp, "\n{ rank=same;\n");
	fprintf_leaf_nodes(dotoutfp, all_routes);
	fprintf(dotoutfp, "}\n");

	free_nodes(all_routes);
	fprintf(dotoutfp,"}\n");

	if (logfp)              fclose(logfp);
	if (urlsfp != stdin)    fclose(urlsfp);
	if (dotoutfp != stdout) fclose(dotoutfp);

	return 0;
}
