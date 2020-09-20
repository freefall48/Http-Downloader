#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>

#include "http.h"
#include "queue.h"

#define FILE_SIZE 256

typedef struct {
    char *url;
    int min_range;
    int max_range;
    Buffer *result;
    int fd;
}  Task;


typedef struct {
    Queue *todo;

    pthread_t *threads;
    int num_workers;

} Context;

void create_directory(const char *dir) {
    struct stat st = { 0 };

    if (stat(dir, &st) == -1) {
        int rc = mkdir(dir, 0700);
        if (rc == -1) {
            perror("mkdir");
            exit(EXIT_FAILURE);
        }
    }
}


void free_task(Task *task) {

    if (task->result) {
        free(task->result->data);
        free(task->result);
    }

    free(task->url);
    free(task);
}


void *worker_thread(void *arg) {
    Context *context = (Context *)arg;

    Task *task = (Task *)queue_get(context->todo);
    char *range = (char *)malloc(1024);
    
    while (task) {
        snprintf(range, 1024, "%d-%d", task->min_range, 
        task->max_range);
    
        task->result = http_url(task->url, range);

        if (task->result) {
            char *data = http_get_content(task->result);
            if (data) {
                size_t length = task->result->length - (data - task->result->data);
                printf("downloaded %d bytes from %s\n", (int)length, task->url);
                if ((pwrite(task->fd, data, length, task->min_range)) <= 0) {
                    perror("pwrite");
                    printf("HEREE\n");
                };
            } else {
                fprintf(stderr, "error downloading: %s\n", task->url);
            }
        } else {
            fprintf(stderr, "error downloading: %s\n", task->url);
        }
        
        free_task(task);

        task = (Task *)queue_get(context->todo);
    }
    
    free(range);
    return NULL;
}


Context *spawn_workers(int num_workers) {
    Context *context = malloc(sizeof(Context));

    context->todo = queue_alloc(num_workers * 2);

    context->num_workers = num_workers;

    context->threads = (pthread_t*)malloc(sizeof(pthread_t) * num_workers);
    int i = 0;

    for (i = 0; i < num_workers; ++i) {
        if (pthread_create(&context->threads[i], NULL, worker_thread, context) != 0) {
            perror("pthread_create");
            exit(1);
        }
    }

    return context;
}

void free_workers(Context *context) {
    int num_workers = context->num_workers;
    int i = 0;

    for (i = 0; i < num_workers; ++i) {
        queue_put(context->todo, NULL);
    }

    for (i = 0; i < num_workers; ++i) {
        if (pthread_join(context->threads[i], NULL) != 0) {
            perror("pthread_join");
            exit(1);
        }
    }

    queue_free(context->todo);

    free(context->threads);
    free(context);
}


Task *new_task(char *url, int min_range, int max_range, int fd) {
    Task *task = malloc(sizeof(Task));
    task->result = NULL;
    task->url = malloc(strlen(url) + 1);
    task->min_range = min_range;
    task->max_range = max_range;
    task->fd = fd;

    strcpy(task->url, url);

    return task;
}

int open_file_output_fd(const char *url, const char* output_dir) {
    char file_path[FILE_SIZE], *cwd, *current, *prev, *context;
    int fd;

    snprintf(file_path, FILE_SIZE, "%s/%s", output_dir, url);

    current = strtok_r(file_path, "/", &context);
    if ((cwd = getcwd(NULL, 0)) == NULL) {
        perror("getcwd");
        return -1;
    }

    while (current != NULL) {
        prev = current;
        current = strtok_r(NULL, "/", &context);
        if (current != NULL)
        {
            create_directory(prev);
            chdir(prev);
        } 
    }

    chdir(cwd);
    free(cwd);

    if (snprintf(file_path, FILE_SIZE, "%s/%s", output_dir, url) < 0) {
        perror("snprintf file_path");
        return -1;
    }

    // // Open a file descriptor to the desired file.
    if ((fd = open(file_path, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR)) < 0) {
            perror("creat output file");
            return -1;
        }

    return fd;
}


int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: ./downloader url_file num_workers download_dir\n");
        exit(1);
    }

    char *url_file = argv[1];
    int num_workers = atoi(argv[2]);
    char *download_dir = argv[3];

    // create_directory(download_dir);
    FILE *fp = fopen(url_file, "r");
    char *line = NULL;
    size_t len = 0;

    if (fp == NULL) {
        exit(EXIT_FAILURE);
    }
    // spawn threads and create work queue(s)
    Context *context = spawn_workers(num_workers);

    while ((len = getline(&line, &len, fp)) != -1) {
        int bytes, num_tasks, fd;

        if (line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        num_tasks = get_num_tasks(line, num_workers);
        bytes = get_max_chunk_size();

        if ((fd = open_file_output_fd(line, download_dir)) <= 0) {
            fprintf(stderr, "Failed to open output file for writing\n");
            continue;
        }
        
        for (int i  = 0; i < num_tasks; i ++) {
            queue_put(context->todo, new_task(line, i * bytes, (i+1) * bytes, fcntl(fd, F_DUPFD_CLOEXEC, 0)));
        }

        close(fd);
    }
   

    //cleanup
    fclose(fp);
    free(line);

    free_workers(context);

    return 0;
}
