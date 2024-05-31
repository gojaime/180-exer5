#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

void error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

ssize_t read_all(int socket, void *buffer, size_t size) {
    size_t total_read = 0;
    ssize_t bytes_read;

    while (total_read < size) {
        bytes_read = read(socket, (int *)buffer + total_read, size - total_read);
        if (bytes_read <= 0) {
            return -1;
        }
        total_read += bytes_read;

    }

    return total_read;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s n p s \n", argv[0]);
        return EXIT_FAILURE;
    }

    int n = atoi(argv[1]);
    int port = atoi(argv[2]);
    int s = atoi(argv[3]);

    if (s == 0) {
        // master mode
        FILE* fp;
        int bufferLength = 255;
        char buffer[bufferLength];

        fp = fopen("slaves.txt", "r");
        if (fp == NULL)
            exit(EXIT_FAILURE);

        // fetch t from first line
        fgets(buffer, bufferLength, fp);
        buffer[strcspn(buffer, "\n")] = 0;
        int t = atoi(buffer);
        printf("\nT = %i\n", t);

        // initialize array to split
        int *array;
        array = (int *)malloc(n * n * sizeof(int));

        for (int i = 0; i < n * n; i++) {
            array[i] = (rand() % 99) + 1;
        }

        // initialize index
        int chunk_size = n / t * n;
        int startingIndex = 0;
        int endingIndex = chunk_size;
        int threadIndex = 1;

        // loop through slave IPs
        while (fgets(buffer, bufferLength, fp)) {
            printf("\nSending to %s\n", buffer);

            // remove trailing newline
            buffer[strcspn(buffer, "\n")] = 0;

            char *ip_port = strtok(buffer, ":");
            char *port_str = strtok(NULL, ":");

            int sock = 0;
            struct sockaddr_in serv_addr;
            if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                printf("\n Socket creation error \n");
                return -1;
            }

            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(atoi(port_str));

            if (inet_pton(AF_INET, ip_port, &serv_addr.sin_addr) <= 0) {
                printf("\nInvalid address/ Address not supported \n");
                return -1;
            }

            if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
                printf("\nConnection Failed \n");
                return -1;
            }

            // send the number of rows
            int *rows;
            rows = (int *)malloc(sizeof(int));


            rows[0] = threadIndex == t? chunk_size/n : n/t;
            // printf("\nRows = %d\n",rows[0]);

            // initialize submatrix
            int *submatrix;
            submatrix = (int *)malloc(chunk_size * sizeof(int));
            int currIndex = 0;
            for (int i = startingIndex; i < endingIndex; i++) {
                submatrix[currIndex] = array[i];
                currIndex++;
            }
            startingIndex += chunk_size;
            if (threadIndex == t-1) {
                endingIndex = n * n;
                chunk_size = (n*n) - ((threadIndex) * chunk_size);


                // printf("\n last index %d\n", endingIndex);
            } else {
                endingIndex += chunk_size;
                // printf("\n not yet\n");
            }


            // time_before
            clock_t t;
            t = clock();

            send(sock, rows, sizeof(int), 0);
            send(sock, submatrix, chunk_size * sizeof(int), 0);
            printf("Array sent\n");

            // wait for ack from slave
            char ack_msg[4] = "ack";
            char recv_ack[4];
            recv(sock, recv_ack, 4, 0);
            if (strcmp(recv_ack, ack_msg) != 0) {
                printf("Error: Unexpected ack message from slave\n");
                return -1;
            }
            else printf("Received ack from %s\n",buffer);

            threadIndex++;
            free(submatrix);
            close(sock);
        }

        // time_after
        t = clock() - t;

        // show sent array

        // for(int i=0; i<n*n; i++){
        //     if(i%n==0){
        //         printf("\n");
        //     }
        //     printf("%d ", array[i]);
        // }

        double time_taken = ((double)t)/CLOCKS_PER_SEC;
        printf("\nElapsed time: %f seconds\n", time_taken);

        free(array);

    } else if (s == 1) {
        // slave mode
        int server_fd, new_socket;
        struct sockaddr_in address;
        int opt = 1;
        int addrlen = sizeof(address);

        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
            perror("socket failed");
            exit(EXIT_FAILURE);
        }

        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
            perror("setsockopt");
            exit(EXIT_FAILURE);
        }

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
            perror("bind failed");
            exit(EXIT_FAILURE);
        }

        printf("Listening on port %d\n",port);

        if (listen(server_fd, 3) < 0) {
            perror("listen");
            exit(EXIT_FAILURE);
        }

        // time_before
        clock_t t;
        t = clock();

        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        int *rows;
        rows = (int *)malloc(sizeof(int));

        read_all(new_socket, rows, sizeof(int));
        printf("Rows: %i\n", rows[0]);


        int chunk_size = rows[0] * n;
        int *buffer;
        buffer = (int *)malloc(chunk_size * sizeof(int));


        read_all(new_socket, buffer, chunk_size * sizeof(int));



        // show received array
        // printf("Array received: \n");
        // for (int i = 0; i < chunk_size; i++) {
        //     if (i % n == 0) {
        //         printf("\n");
        //     }
        //     printf("%i ", buffer[i]);
        // }
        // printf("\n");

        // send ack back to master
        send(new_socket, "ack", 4, 0);

        printf("\nSent ack to master\n");

        // time_after
        t = clock() - t;

        close(new_socket);
        close(server_fd);

        double time_taken = ((double)t)/CLOCKS_PER_SEC;
        printf("\nElapsed time: %fs\n", time_taken);
        printf("Press enter to exit...\n");
        char dummy;
        scanf("%c", &dummy);
    } else {
        fprintf(stderr, "Invalid value for s. Use 0 for master and 1 for slave.\n");
        return EXIT_FAILURE;
    }

    return 0;
}
