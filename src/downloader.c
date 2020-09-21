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
        snprintf(range, 1024, "%d-%d", task->min_range, task->max_range);
        task->result = http_url(task->url, range);

        if (task->result) {
            // Strip the header information from the Buffer.
            char *data = http_get_content(task->result);
            if (data) {
                size_t length = task->result->length - (data - task->result->data);
                printf("downloaded %d bytes from %s\n", (int)length, task->url);

                // Write the Buffer to the provided file descriptor. pwrite() is thread-safe
                // and can write to a file with an offset. This task has downloaded the bytes
                // for its byte range, therefore, its byte range is unique. The minimum of the
                // byte range is the offset to start writing at which will not confict with other
                // concurrent write requests to the file.
                if ((pwrite(task->fd, data, length, task->min_range)) <= 0) {
                    fprintf(stderr, "error writing: %s\n", task->url);
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

    // Prefix the download path with the user specified directory
    snprintf(file_path, FILE_SIZE, "%s/%s", output_dir, url);

    // Get the current working directory so it can be returned to
    // after the directory tree is created.
    if ((cwd = getcwd(NULL, 0)) == NULL) {
        perror("getcwd");
        return -1;
    }

    // While the file url still contains '/' characters, there must
    // be more subdirectories.
    current = strtok_r(file_path, "/", &context);
    while (current != NULL) {
        prev = current;
        current = strtok_r(NULL, "/", &context);
        if (current != NULL)
        {
            // There is atleast another level directory level below this one
            // so create this one and enter it.
            create_directory(prev);
            chdir(prev);
        } 
    }

    // Reset back to the working directory the program was prevously 
    // executing within.
    chdir(cwd);
    free(cwd);

    // Reset the file_url back to the full path. strtok_r placed '\0' in the
    // previous copy to aid in creating the directory tree.
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

    // Foreach url within the file that contains a list of urls to download.
    while ((len = getline(&line, &len, fp)) != -1) {
        int bytes, num_tasks, fd;

        if (line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        // Determine the number of downloads required to completely retrieve the
        // specified file. Validates the returned value.
        if ((num_tasks = get_num_tasks(line, num_workers)) <= 1) {
            // The number of required downloads could not be determined.
            continue;
        }
        // As the above call must of returned a valid number of downloads, get
        // the determined chunk size.
        bytes = get_max_chunk_size();

        // Open a file descriptor for the given url where the downlaoded bytes can be
        // written.
        if ((fd = open_file_output_fd(line, download_dir)) <= 0) {
            // The file descriptor was never created/assigned.
            fprintf(stderr, "Failed to open output file for writing\n");
            continue;
        }
        
        // For each download required for a given url, create a new task with the required 
        // byte range. fcntl is used to duplicate the file descriptor so each task has
        // its own unique reference to the file.
        // F_DUPFD_CLOEXEC ensures once a task writes to the file descriptor it is automatically closed.
        for (int i  = 0; i < num_tasks; i ++) {
            queue_put(context->todo, new_task(line, i * bytes, (i+1) * bytes, fcntl(fd, F_DUPFD_CLOEXEC, 0)));
        }

        // Cleanup
        close(fd);
    }
   

    //cleanup
    fclose(fp);
    free(line);

    free_workers(context);

    return 0;
}
