#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <pthread.h>

void error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

ssize_t read_all(int socket, void *buffer, size_t size) {
    size_t total_read = 0;
    ssize_t bytes_read;

    while (total_read < size) {
        bytes_read = read(socket, (float *)buffer + total_read, size - total_read);
        if (bytes_read <= 0) {
            return -1;
        }
        total_read += bytes_read;
    }

    return total_read;
}

float pearson_cor(float *x, float *y, int n) {
    float sum_x = 0, sum_y = 0, sum_xy = 0;
    float sum_x2 = 0, sum_y2 = 0;

    for (int i = 0; i < n; i++) {
        sum_x += x[i];
        sum_y += y[i];
        sum_xy += x[i] * y[i];
        sum_x2 += x[i] * x[i];
        sum_y2 += y[i] * y[i];
    }

    float numerator = n * sum_xy - sum_x * sum_y;
    float denominator = sqrt((n * sum_x2 - sum_x * sum_x) * (n * sum_y2 - sum_y * sum_y));
    if (denominator == 0) return 0; // Prevent division by zero
    return numerator / denominator;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s n port s\n", argv[0]);
        return EXIT_FAILURE;
    }

    int n = atoi(argv[1]);
    int port = atoi(argv[2]);
    int s = atoi(argv[3]);

    if (s == 0) {
        // master mode
        FILE *fp;
        int bufferLength = 255;
        char buffer[bufferLength];

        fp = fopen("slaves.txt", "r");
        if (fp == NULL) error("Failed to open slaves.txt");

        // fetch t from first line
        if (fgets(buffer, bufferLength, fp) == NULL) error("Failed to read from slaves.txt");
        buffer[strcspn(buffer, "\n")] = 0;
        int t = atoi(buffer);
        printf("T = %i\n", t);

        // initialize array to split
        float *array = (float *)malloc(n * n * sizeof(float));
        if (array == NULL) error("Failed to allocate memory for array");

        for (int i = 0; i < n * n; i++) {
            array[i] = (rand() % 99) + 1;
        }

        // initialize y vector
        float *y = (float *)malloc(n * sizeof(float));
        if (y == NULL) error("Failed to allocate memory for y vector");

        for (int i = 0; i < n; i++) {
            y[i] = (rand() % 99) + 1;
        }

        // initialize index
        int chunk_size = n / t * n;
        int startingIndex = 0;
        int endingIndex = chunk_size;
        int threadIndex = 1;

        // loop through slave IPs
        while (fgets(buffer, bufferLength, fp)) {
            printf("Sending to %s\n", buffer);

            // remove trailing newline
            buffer[strcspn(buffer, "\n")] = 0;

            char *ip_port = strtok(buffer, ":");
            char *port_str = strtok(NULL, ":");

            int sock = 0;
            struct sockaddr_in serv_addr;
            if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                error("Socket creation error");
            }

            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(atoi(port_str));

            if (inet_pton(AF_INET, ip_port, &serv_addr.sin_addr) <= 0) {
                error("Invalid address/ Address not supported");
            }

            if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
                error("Connection Failed");
            }

            float rows = (threadIndex == t) ? (chunk_size) / n : n / t;

            // initialize submatrix
            float *submatrix = (float *)malloc(chunk_size * sizeof(float));
            if (submatrix == NULL) error("Failed to allocate memory for submatrix");

            int currIndex = 0;
            for (int i = startingIndex; i < endingIndex; i++) {
                submatrix[currIndex++] = array[i];
            }

            startingIndex += chunk_size;
            if (threadIndex == t - 1) {
                endingIndex = n * n;
                chunk_size = (n * n) - ((threadIndex) * chunk_size);
            } else {
                endingIndex += chunk_size;
            }

            // time_before
            clock_t t_start = clock();

            // send rows
            send(sock, &rows, sizeof(float), 0);
            // send submatrix
            send(sock, submatrix, chunk_size * sizeof(float), 0);
            printf("Array sent\n");
            // send y vector
            send(sock, y, n * sizeof(float), 0);

            // wait for ack from slave
            char ack_msg[4] = "ack";
            char recv_ack[4];
            recv(sock, recv_ack, 4, 0);
            if (strcmp(recv_ack, ack_msg) != 0) {
                error("Unexpected ack message from slave");
            } else {
                printf("Received ack from %s\n", buffer);
            }

            // wait for r vector
            // Code for receiving the r vector from slave can be added here

            threadIndex++;
            free(submatrix);
            close(sock);

            // time_after
            double time_taken = ((double)(clock() - t_start)) / CLOCKS_PER_SEC;
            printf("Elapsed time: %f seconds\n", time_taken);
        }

        free(array);
        free(y);
        fclose(fp);

    } else if (s == 1) {
        // slave mode
        int server_fd, new_socket;
        struct sockaddr_in address;
        int opt = 1;
        int addrlen = sizeof(address);

        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
            error("Socket creation failed");
        }

        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
            error("setsockopt failed");
        }

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
            error("Bind failed");
        }

        printf("Listening on port %d\n", port);

        if (listen(server_fd, 3) < 0) {
            error("Listen failed");
        }

        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            error("Accept failed");
        }

        // receive rows
        float rows;
        read_all(new_socket, &rows, sizeof(float));
        printf("Rows: %f\n", rows);
        int chunk_size = rows * n;

        // receive y vector
        float *y_vector = (float *)malloc(n * sizeof(float));
        if (y_vector == NULL) error("Failed to allocate memory for y vector");
        read_all(new_socket, y_vector, n * sizeof(float));

        // receive submatrix
        float *buffer = (float *)malloc(chunk_size * sizeof(float));
        if (buffer == NULL) error("Failed to allocate memory for buffer");
        read_all(new_socket, buffer, chunk_size * sizeof(float));

        // send ack back to master
        send(new_socket, "ack", 4, 0);
        printf("Sent ack to master\n");

        // time_before
        clock_t t_start = clock();

        // calculate Pearson correlation for each row
        float *correlation_results = (float *)malloc(rows * sizeof(float));
        if (correlation_results == NULL) error("Failed to allocate memory for correlation results");

        for (int i = 0; i < rows; i++) {
            correlation_results[i] = pearson_cor(&buffer[i * n], y_vector, n);
        }

        // send correlation results back to master
        send(new_socket, correlation_results, rows * sizeof(float), 0);

        // time_after
        double time_taken = ((double)(clock() - t_start)) / CLOCKS_PER_SEC;
        printf("Elapsed time: %f seconds\n", time_taken);

        // clean up
        free(y_vector);
        free(buffer);
        free(correlation_results);
        close(new_socket);
        close(server_fd);

        printf("Press enter to exit...\n");
        getchar();
    } else {
        fprintf(stderr, "Invalid value for s. Use 0 for master and 1 for slave.\n");
        return EXIT_FAILURE;
    }

    return 0;
}
