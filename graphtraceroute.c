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

struct tracenode;
struct tracenode {
	int kbs;
	char* hostname;
	int num_children;
	struct tracenode** children;
};

FILE* consolefp;

struct tracenode* all_routes;

struct tracenode* ___create_node(char* hostname, int kbs) {
// 	fprintf(stderr, "Creating node %s\n", hostname);
	struct tracenode* ptr = (struct tracenode*)malloc(sizeof(struct tracenode));
	memset(ptr, 0, sizeof(struct tracenode));
	ptr->hostname = hostname;
	ptr->kbs = kbs;	
	return ptr;
}

void ___add_trace(struct tracenode* node, char** trace, int numhosts, int kbs) {
	assert(node->hostname != NULL);

	if (*trace == NULL)
		return;

	if (numhosts == 0)
		return;
	
	if (node->kbs < kbs)
		node->kbs = kbs;

	// If current node matches
	if (strcmp(node->hostname, *trace) == 0) { // Hostname-match traverse to child nodes
		int i;
		// Search children for match
		trace++;
		for (i=0; i<node->num_children; i++) {
			if (strcmp(node->children[i]->hostname, *trace) == 0) {
				___add_trace(node->children[i], trace, numhosts-1, kbs);
				return;
			}
		}
	}

	if (!node->children)
		node->children = malloc((node->num_children+1)*sizeof(struct tracenode*));
	else
		node->children = realloc(node->children, (node->num_children+1)*sizeof(struct tracenode*));
	node->children[node->num_children] = ___create_node(*trace, kbs);
	node->num_children ++;

	___add_trace(node->children[node->num_children-1], &trace[1], numhosts-1, kbs);
}


void add_trace(char** trace, int numhosts, int kbs) {
	if (!all_routes) { // Create initial localhost node
		all_routes = ___create_node(*trace, kbs);
	}
	numhosts--;

// 	fprintf(stderr, "Traversing nodes \n");
	___add_trace(all_routes, trace, numhosts, kbs);
}

void fprintf_nodes(FILE* fp, struct tracenode* node) {
	const int kbs_win  = 700;
	const int kbs_fail = 400;
	int i;

	if (!node)
		return;

	for (i=0; i<node->num_children; i++) {
		unsigned char R = 0;
		unsigned char G = 0;
		unsigned char B = 0;
		int kbs = node->children[i]->kbs;
		if (node->children[i]->num_children == 0) {
			fprintf(fp,"\"%s\" [shape=box];\n", node->children[i]->hostname);
		}

		if (kbs >= kbs_win)
			G = 0xff;
		else if (kbs >= kbs_fail) {
			unsigned int val = ((kbs - kbs_fail) * 512) / (kbs_win - kbs_fail);
			if (val > 255) {
				G = val - 255;
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

		fprintf(fp,"\"%s\" -> \"%s\";\n", node->hostname, node->children[i]->hostname);
	}

	for (i=0; i<node->num_children; i++)
		fprintf_nodes(fp, node->children[i]);
}

void fprintf_leaf_nodes(FILE* fp, struct tracenode* node) {
	int i;

	if (!node)
		return;

	if (node->num_children != 0) {
		for (i=0; i<node->num_children; i++)
			fprintf_leaf_nodes(fp, node->children[i]);
	} else {
		fprintf(fp,"\"%s\";\n",node->hostname);
	}
}


void free_nodes(struct tracenode* node) {
	int i;

	if (!node)
		return;
	for (i=0; i<node->num_children; i++)
		free_nodes(node->children[i]);

	free(node->children);
	free(node->hostname);
}


char url2host(const char* url, char* host, int hostlen) {
	const char* ptr = strstr(url,"//");
	
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

void tracehost(const char* host, int kbs) {
	char* traceroute[64];
	char buffer[1024];
	FILE* fp;
	int i;
	int status;
	int max_hop = 0;

	memset(traceroute, 0, sizeof(traceroute));

	strcpy(buffer, "mtr --raw -c 5 ");
	strcat(buffer, host);
	strcat(buffer, " 2> /dev/null");

	/* Spawn mtr */
	fprintf(consolefp, "Tracing %s   ", host);
	fp = popen(buffer, "r");
	if (fp == NULL) {
		fprintf(stderr, "popen error\n");
		exit(1);
	}

	int progress = 0;
	const char progress_str[] = {'-','\\','|','/'};
	while ( fgets(buffer, sizeof(buffer), fp) != NULL) {
		int hop;
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
			if (traceroute[hop] != NULL) {
// 				fprintf(stderr, "Freeing hop %d\n", hop);
				free(traceroute[hop]);
			}
			traceroute[hop] = strdup(tmpbuf);
// 			fprintf(stderr, "Setting hop %d %s\n", hop, traceroute[hop]);
		}

		if (cmd == 'p' && hop == 0) {
			if (traceroute[max_hop+1] != NULL) {
// 				fprintf(stderr, "Freeing hop %d\n", max_hop+1);
				free(traceroute[max_hop+1]);
				traceroute[max_hop+1] = NULL;
			}
			max_hop = 0;
		}
	}
	if (consolefp) fprintf(consolefp, "\n");

	status = pclose(fp);
	if (status == -1) {
		fprintf(stderr,"pclose error\n");
		exit(1);
	}

	for (i=0; i<64; i++) {
		if (i>0 && !traceroute[i-1] && traceroute[i]) {
			traceroute[i-1] =  traceroute[i];
			traceroute[i] = NULL;
			i = 0;
		}
	}

	/* Put trace into tree */
	max_hop = 0;
	for (i=0; i<64; i++) {
		if (traceroute[i] == NULL)
			break;
		max_hop	++;
// 		fprintf(stderr,"HOST%d: %s\n", max_hop, traceroute[i]);
	}
	// Add url host at the end
// 	fprintf(stderr, "¤¤¤¤ %s %s\n", traceroute[max_hop-1], host);
	if (strcmp(traceroute[max_hop-1], host) != 0) {
		traceroute[max_hop] = strdup(host);
		max_hop++;
	}
	add_trace(traceroute, max_hop, kbs);
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

	int progress = 0;
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
					speed_history = realloc(speed_history, (history_length+1)*(sizeof(struct host_speed)));

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
		int len = strlen(url);
		while (!isalnum(url[len-1])) {
			len --;
			url[len] = 0;
		}
		url2host(url, host, sizeof(host));
		int kbs = getspeed(url);
		const struct host_speed* sh = speed_history_max_kbs(host);
		if (logfp)
			fprintf(logfp, "%s %d\n", host, kbs);
		
		if (sh && sh->kbs > kbs)
			kbs = sh->kbs;
		tracehost(host, kbs);
		idx++;
	}

	fprintf(dotoutfp,"digraph A  { size=\"1000,1000\"; \n");
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
