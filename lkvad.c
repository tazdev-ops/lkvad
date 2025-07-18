#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>
#include <curl/curl.h>
#include <pthread.h>

#define MAX_URL_LENGTH 2048
#define DEFAULT_TIMEOUT 5

typedef enum {
    FORMAT_PLAIN,
    FORMAT_M3U,
    FORMAT_M3U8,
    FORMAT_PLS,
    FORMAT_XSPF
} playlist_format_t;

typedef struct {
    char *link_template;
    char *playlist_file;
    int start;
    int end;
    int padding;
    playlist_format_t format;
    bool verify_urls;
    bool verbose;
    int threads;
    char *prefix_text;
    char *suffix_text;
} config_t;

typedef struct {
    char *url;
    bool is_valid;
    int index;
} url_check_t;

// CURL write callback to discard data
size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    return size * nmemb;
}

// Check if URL is accessible
bool check_url(const char *url) {
    CURL *curl = curl_easy_init();
    if (!curl) return false;
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, DEFAULT_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    CURLcode res = curl_easy_perform(curl);
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);
    
    return (res == CURLE_OK && response_code >= 200 && response_code < 400);
}

// Thread function for parallel URL checking
void *check_url_thread(void *arg) {
    url_check_t *check = (url_check_t *)arg;
    check->is_valid = check_url(check->url);
    return NULL;
}

void print_usage(const char *prog_name) {
    fprintf(stderr, "Enhanced Playlist Generator v2.0\n");
    fprintf(stderr, "Usage: %s [OPTIONS]\n\n", prog_name);
    fprintf(stderr, "Required options:\n");
    fprintf(stderr, "  -l <template>    URL template with wildcard (*)\n");
    fprintf(stderr, "  -s <start>       Starting number\n");
    fprintf(stderr, "  -e <end>         Ending number\n");
    fprintf(stderr, "  -p <file>        Output playlist file\n\n");
    fprintf(stderr, "Optional options:\n");
    fprintf(stderr, "  -f <format>      Playlist format: plain|m3u|m3u8|pls|xspf (default: plain)\n");
    fprintf(stderr, "  -z <padding>     Zero-pad numbers (e.g., -z 3 for 001, 002, ...)\n");
    fprintf(stderr, "  -v               Verify URLs (check if accessible)\n");
    fprintf(stderr, "  -V               Verbose output\n");
    fprintf(stderr, "  -t <threads>     Number of threads for URL verification (default: 4)\n");
    fprintf(stderr, "  -P <prefix>      Add prefix text to each entry\n");
    fprintf(stderr, "  -S <suffix>      Add suffix text to each entry\n\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  %s -l \"http://example.com/episode_*.mp3\" -s 1 -e 10 -p playlist.m3u -f m3u\n", prog_name);
    fprintf(stderr, "  %s -l \"http://cdn.example.com/video_*.mp4\" -s 1 -e 100 -p videos.m3u8 -f m3u8 -z 3 -v\n", prog_name);
}

playlist_format_t parse_format(const char *format_str) {
    if (!format_str) return FORMAT_PLAIN;
    
    if (strcasecmp(format_str, "m3u") == 0) return FORMAT_M3U;
    if (strcasecmp(format_str, "m3u8") == 0) return FORMAT_M3U8;
    if (strcasecmp(format_str, "pls") == 0) return FORMAT_PLS;
    if (strcasecmp(format_str, "xspf") == 0) return FORMAT_XSPF;
    
    return FORMAT_PLAIN;
}

void write_playlist_header(FILE *file, playlist_format_t format, int total_entries) {
    switch (format) {
        case FORMAT_M3U:
        case FORMAT_M3U8:
            fprintf(file, "#EXTM3U\n");
            break;
        case FORMAT_PLS:
            fprintf(file, "[playlist]\n");
            fprintf(file, "NumberOfEntries=%d\n", total_entries);
            fprintf(file, "Version=2\n\n");
            break;
        case FORMAT_XSPF:
            fprintf(file, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
            fprintf(file, "<playlist version=\"1\" xmlns=\"http://xspf.org/ns/0/\">\n");
            fprintf(file, "  <trackList>\n");
            break;
        default:
            break;
    }
}

void write_playlist_entry(FILE *file, playlist_format_t format, const char *url, 
                         int index, const char *title, const char *prefix, const char *suffix) {
    char full_url[MAX_URL_LENGTH];
    snprintf(full_url, sizeof(full_url), "%s%s%s", 
             prefix ? prefix : "", url, suffix ? suffix : "");
    
    switch (format) {
        case FORMAT_M3U:
        case FORMAT_M3U8:
            fprintf(file, "#EXTINF:-1,%s\n", title ? title : url);
            fprintf(file, "%s\n", full_url);
            break;
        case FORMAT_PLS:
            fprintf(file, "File%d=%s\n", index, full_url);
            fprintf(file, "Title%d=%s\n", index, title ? title : url);
            fprintf(file, "Length%d=-1\n\n", index);
            break;
        case FORMAT_XSPF:
            fprintf(file, "    <track>\n");
            fprintf(file, "      <location>%s</location>\n", full_url);
            if (title) {
                fprintf(file, "      <title>%s</title>\n", title);
            }
            fprintf(file, "    </track>\n");
            break;
        default:
            fprintf(file, "%s\n", full_url);
            break;
    }
}

void write_playlist_footer(FILE *file, playlist_format_t format) {
    if (format == FORMAT_XSPF) {
        fprintf(file, "  </trackList>\n");
        fprintf(file, "</playlist>\n");
    }
}

char *generate_url(const char *prefix, const char *suffix, int number, int padding) {
    char *url = malloc(MAX_URL_LENGTH);
    if (!url) return NULL;
    
    if (padding > 0) {
        char format_str[16];
        snprintf(format_str, sizeof(format_str), "%%s%%0%dd%%s", padding);
        snprintf(url, MAX_URL_LENGTH, format_str, prefix, number, suffix);
    } else {
        snprintf(url, MAX_URL_LENGTH, "%s%d%s", prefix, number, suffix);
    }
    
    return url;
}

int main(int argc, char *argv[]) {
    config_t config = {
        .link_template = NULL,
        .playlist_file = NULL,
        .start = 0,
        .end = 0,
        .padding = 0,
        .format = FORMAT_PLAIN,
        .verify_urls = false,
        .verbose = false,
        .threads = 4,
        .prefix_text = NULL,
        .suffix_text = NULL
    };
    
    int c;
    char *format_str = NULL;
    
    // Parse command-line options
    while ((c = getopt(argc, argv, "l:s:e:p:f:z:vVt:P:S:h")) != -1) {
        switch (c) {
            case 'l':
                config.link_template = optarg;
                break;
            case 's':
                config.start = atoi(optarg);
                break;
            case 'e':
                config.end = atoi(optarg);
                break;
            case 'p':
                config.playlist_file = optarg;
                break;
            case 'f':
                format_str = optarg;
                break;
            case 'z':
                config.padding = atoi(optarg);
                break;
            case 'v':
                config.verify_urls = true;
                break;
            case 'V':
                config.verbose = true;
                break;
            case 't':
                config.threads = atoi(optarg);
                if (config.threads < 1) config.threads = 1;
                break;
            case 'P':
                config.prefix_text = optarg;
                break;
            case 'S':
                config.suffix_text = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // Validate required arguments
    if (!config.link_template || !config.playlist_file || config.start <= 0 || config.end <= 0) {
        fprintf(stderr, "Error: Missing required arguments.\n\n");
        print_usage(argv[0]);
        return 1;
    }
    
    if (config.start > config.end) {
        fprintf(stderr, "Error: Start value cannot be greater than end value.\n");
        return 1;
    }
    
    // Parse format
    config.format = parse_format(format_str);
    
    // Initialize CURL if URL verification is enabled
    if (config.verify_urls) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    
    // Find wildcard and split template
    char *asterisk = strchr(config.link_template, '*');
    if (!asterisk) {
        fprintf(stderr, "Error: No wildcard (*) found in template.\n");
        return 1;
    }
    
    char *link_prefix = strndup(config.link_template, asterisk - config.link_template);
    char *link_suffix = strdup(asterisk + 1);
    
    if (!link_prefix || !link_suffix) {
        fprintf(stderr, "Error: Memory allocation failed.\n");
        return 1;
    }
    
    // Open output file
    FILE *file = fopen(config.playlist_file, "w");
    if (!file) {
        perror("Error opening output file");
        free(link_prefix);
        free(link_suffix);
        return 1;
    }
    
    // Calculate total entries
    int total_entries = config.end - config.start + 1;
    
    // Write playlist header
    write_playlist_header(file, config.format, total_entries);
    
    // Generate URLs
    int valid_count = 0;
    int invalid_count = 0;
    
    printf("Generating playlist with %d entries...\n", total_entries);
    
    for (int i = config.start; i <= config.end; i++) {
        char *url = generate_url(link_prefix, link_suffix, i, config.padding);
        if (!url) {
            fprintf(stderr, "Error: Memory allocation failed for URL.\n");
            continue;
        }
        
        bool is_valid = true;
        
        // Verify URL if requested
        if (config.verify_urls) {
            if (config.verbose) {
                printf("Checking: %s", url);
                fflush(stdout);
            }
            
            is_valid = check_url(url);
            
            if (config.verbose) {
                printf(" [%s]\n", is_valid ? "OK" : "FAILED");
            }
            
            if (is_valid) {
                valid_count++;
            } else {
                invalid_count++;
            }
        }
        
        // Write to playlist if valid or verification not requested
        if (is_valid || !config.verify_urls) {
            char title[256];
            snprintf(title, sizeof(title), "Track %d", i);
            write_playlist_entry(file, config.format, url, i - config.start + 1, 
                               title, config.prefix_text, config.suffix_text);
        }
        
        free(url);
        
        // Show progress
        if (!config.verbose && (i - config.start + 1) % 10 == 0) {
            printf("\rProgress: %d/%d", i - config.start + 1, total_entries);
            fflush(stdout);
        }
    }
    
    if (!config.verbose) {
        printf("\rProgress: %d/%d\n", total_entries, total_entries);
    }
    
    // Write playlist footer
    write_playlist_footer(file, config.format);
    
    // Clean up
    fclose(file);
    free(link_prefix);
    free(link_suffix);
    
    if (config.verify_urls) {
        curl_global_cleanup();
        printf("\nVerification complete: %d valid, %d invalid URLs\n", valid_count, invalid_count);
    }
    
    printf("Playlist file '%s' created successfully.\n", config.playlist_file);
    
    return 0;
}
