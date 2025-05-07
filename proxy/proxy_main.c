/*
 *   This file is part of DroneBridge: https://github.com/seeul8er/DroneBridge
 *
 *   Copyright 2018 Wolfgang Christl
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <zconf.h>
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>
#include <memory.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "proxy_main.h"
#include "../common/db_protocol.h"
#include "../common/db_raw_receive.h"
#include "../common/db_raw_send_receive.h"
#include "../common/tcp_server.h"
#include "../common/mavlink/c_library_v2/mavlink_types.h"
#include "../common/db_common.h"

#define TCP_BUFFER_SIZE (DATA_UNI_LENGTH-DB_RAW_V2_HEADER_LENGTH)
#define MAX_TCP_CLIENTS 10
#define MAX_TIRES_OSD_FIFO_OPEN 10
#define DEFAULT_LOG_PATH "/DroneBridge/log/"
#define MAX_PATH_LENGTH 1000

bool volatile keeprunning = true;
char db_mode, write_to_osdfifo;
uint8_t comm_id = DEFAULT_V2_COMMID, frame_type;
int bitrate_op, prox_adhere_80211, num_interfaces;
char adapters[DB_MAX_ADAPTERS][IFNAMSIZ];
char log_path[MAX_PATH_LENGTH];
uint8_t tel_msg_log_buff[MAVLINK_MAX_PACKET_LEN + sizeof(uint64_t)];

void int_handler(int dummy) {
    keeprunning = false;
}

static inline uint64_t getSystemTimeUsecs()
{
    struct timeval tv;		  //System time
    gettimeofday(&tv, NULL);
    return ((uint64_t)tv.tv_sec) * 1000000 + tv.tv_usec;
}

void process_command_line_args(int argc, char *argv[]) {
    db_mode = DEFAULT_DB_MODE;
    write_to_osdfifo = 'Y';
    opterr = 0;
    num_interfaces = 0;
    bitrate_op = 1;
    prox_adhere_80211 = 0;
    frame_type = DB_FRAMETYPE_DEFAULT;
    strcpy(log_path, DEFAULT_LOG_PATH);
    int c;
    while ((c = getopt(argc, argv, "n:m:c:b:o:f:a:l:?")) != -1) {
        switch (c) {
            case 'n':
                if (num_interfaces < DB_MAX_ADAPTERS) {
                    strncpy(adapters[num_interfaces], optarg, IFNAMSIZ);
                    num_interfaces++;
                }
                break;
            case 'm':
                db_mode = *optarg;
                break;
            case 'c':
                comm_id = (uint8_t) strtol(optarg, NULL, 10);
                break;
            case 'b':
                bitrate_op = (int) strtol(optarg, NULL, 10);
                break;
            case 'o':
                write_to_osdfifo = *optarg;
                break;
            case 'f':
                frame_type = (uint8_t) strtol(optarg, NULL, 10);
                break;
            case 'l':
                strncpy(log_path, optarg, MAX_PATH_LENGTH);
                break;
            case 'a':
                prox_adhere_80211 = (int) strtol(optarg, NULL, 10);
                break;
            case '?':
                LOG_SYS_STD(LOG_INFO,
                            "DroneBridge Proxy module is used to do any UDP <-> DB_CONTROL_AIR routing. UDP IP given by "
                            "IP-checker module. Use"
                            "\n\t-n [network_IF_proxy_module] "
                            "\n\t-m [w|m] DroneBridge mode - wifi - monitor mode (default: m) (wifi not supported yet!)"
                            "\n\t-c [communication id] Choose a number from 0-255. Same on groundstation and drone!"
                            "\n\t-l Path to store the telemetry log files. Default /DroneBridge/log/"
                            "\n\t-o [Y|N] Write telemetry to /root/telemetryfifo1 FIFO (default: Y)"
                            "\n\t-f [1|2] DroneBridge v2 raw protocol packet/frame type: 1=RTS, 2=DATA (CTS protection)"
                            "\n\t-b bit rate:\tin Mbps (1|2|5|6|9|11|12|18|24|36|48|54)\n\t\t(bitrate option only "
                            "supported with Ralink chipsets)"
                            "\n\t-a [0|1] to disable/enable. Offsets the payload by some bytes so that it sits outside "
                            "then 802.11 header. Set this to 1 if you are using a non DB-Rasp Kernel!");
                break;
            default:
                abort();
        }
    }
    if (strlen(log_path) > 0 && log_path[strlen(log_path) - 1] != '/')
        strcat(log_path, "/");
}

bool file_exists(char fname[]) {
    if( access( fname, F_OK ) != -1 )
        return true;
    else
        return false;
}

struct log_file_t open_telemetry_log_file() {
    struct log_file_t log_file;
    struct tm *timenow;

    time_t now = time(NULL);
    timenow = localtime(&now);
    char file_name[MAX_PATH_LENGTH];
    strftime(file_name, sizeof(file_name), "DB_TELEMETRY_%F_%H%M", timenow);
    strcat(log_path, file_name);
    strcpy(log_file.file_name, log_path);
    if (file_exists(log_file.file_name)) {
        for (int i = 0; i < 10; i++) {
            char str[12];
            sprintf(str, "_%d", i);
            strcat(log_file.file_name, str);
            if (!file_exists(log_file.file_name))
                break;
        }
    }
    log_file.file_pntr = fopen(log_file.file_name, "a");
    if(log_file.file_pntr == NULL)
        perror("DB_PROXY_GROUND: Error opening log file!");
    else
        LOG_SYS_STD(LOG_INFO, "DB_PROXY_GROUND: Opened telemetry log: %s\n", log_file.file_name);
    return log_file;
}

int log_telem_to_file(FILE *file_pnt, uint8_t tel_bytes[], int tel_bytes_length) {
    if (file_pnt != NULL) {
        uint64_t time = getSystemTimeUsecs();
        memcpy(tel_msg_log_buff, (void*)&time, sizeof(uint64_t));
        memcpy(tel_msg_log_buff + sizeof(uint64_t), tel_bytes, tel_bytes_length);
        return fwrite(tel_msg_log_buff, tel_bytes_length + sizeof(uint64_t), 1, file_pnt);
    }
    return 0;
}

int open_osd_fifo() {
    int tries = 0;
    char fifoname[100];
    sprintf(fifoname, "/root/telemetryfifo1");
    // we do only write but O_RDWR allows to open without receiver in non-blocking mode
    int tempfifo_osd = open(fifoname, O_RDWR | O_NONBLOCK);
    // try to open FIFO to OSD for a couple of times. OSD might not start because of no HDMI devide connected -> no FIFO
    while (tempfifo_osd == -1 && tries < MAX_TIRES_OSD_FIFO_OPEN) {
        perror("DB_PROXY_GROUND: Unable to open OSD FIFO. OSD make sure OSD is running");
        LOG_SYS_STD(LOG_INFO, "DB_PROXY_GROUND: Creating FIFO %s\n", fifoname);
        if (mkfifo(fifoname, 0777) < 0)
            perror("Cannot create FIFO");
        tempfifo_osd = open(fifoname, O_RDWR | O_NONBLOCK);
        tries++;
        usleep((__useconds_t) 3e6);
    }
    if (tempfifo_osd == -1)
        LOG_SYS_STD(LOG_ERR, "DB_PROXY_GROUND: Error opening FIFO. Giving up. OSD might not be running because of no "
                             "HDMI DEV\n" );
    return tempfifo_osd;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, int_handler);
    signal(SIGTERM, int_handler);
    signal(SIGPIPE, SIG_IGN);
    usleep((__useconds_t) 1e6);
    process_command_line_args(argc, argv);

    // set up long range sockets
    db_socket_t raw_interfaces[DB_MAX_ADAPTERS] = {0};
    for (int i = 0; i < num_interfaces; ++i) {
        raw_interfaces[i] = open_db_socket(adapters[i], comm_id, db_mode, bitrate_op, DB_DIREC_DRONE, DB_PORT_PROXY,
                                           frame_type);
    }
    int fifo_osd = -1, new_tcp_client;
    int tcp_clients[MAX_TCP_CLIENTS] = {0};
    if (write_to_osdfifo == 'Y') {
        fifo_osd = open_osd_fifo();
    }

    // init variables
    uint16_t radiotap_length = 0;
    fd_set fd_socket_set;
    struct timeval select_timeout;
    size_t recv_length = 0;

    // Setup TCP server for GCS communication
    struct tcp_server_info_t tcp_server_info = create_tcp_server_socket(APP_PORT_PROXY);
    int tcp_addrlen = sizeof(tcp_server_info.servaddr);

    // open log file for messages incoming from long range link
    struct log_file_t log_file = open_telemetry_log_file();

    struct data_uni *data_uni_to_drone = get_hp_raw_buffer(prox_adhere_80211);
    uint8_t seq_num = 0, seq_num_proxy = 0, last_recv_seq_num = 0;
    uint8_t lr_buffer[DATA_UNI_LENGTH];
    uint8_t tcp_buffer[TCP_BUFFER_SIZE];
    size_t payload_length = 0;

    LOG_SYS_STD(LOG_INFO, "DB_PROXY_GROUND: started! Enabled diversity on %i adapters.\n", num_interfaces);
    while (keeprunning) {
        select_timeout.tv_sec = 5;
        select_timeout.tv_usec = 0;
        FD_ZERO (&fd_socket_set);
        FD_SET (tcp_server_info.sock_fd, &fd_socket_set);
        int max_sd = tcp_server_info.sock_fd;
        // add raw DroneBridge sockets
        for (int i = 0; i < num_interfaces; i++) {
            FD_SET (raw_interfaces[i].db_socket, &fd_socket_set);
            if (raw_interfaces[i].db_socket > max_sd)
                max_sd = raw_interfaces[i].db_socket;
        }
        // add child sockets (tcp connection sockets) to set
        for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
            int tcp_client_sd = tcp_clients[i];
            if (tcp_client_sd > 0) {
                FD_SET(tcp_client_sd, &fd_socket_set);
                if (tcp_client_sd > max_sd)
                    max_sd = tcp_client_sd;
            }
        }

        int select_return = select(max_sd + 1, &fd_socket_set, NULL, NULL, &select_timeout);
        if (select_return == -1) {
            perror("DB_PROXY_GROUND: select() returned error: ");
        } else if (select_return > 0) {
            for (int i = 0; i < num_interfaces; i++) {
                if (FD_ISSET(raw_interfaces[i].db_socket, &fd_socket_set)) {
                    // ---------------
                    // incoming form long range proxy port - write data to OSD-FIFO and pass on to connected TCP clients
                    // ---------------
                    ssize_t l = recv(raw_interfaces[i].db_socket, lr_buffer, DATA_UNI_LENGTH, 0);
                    int err = errno;
                    if (l > 0) {
                        payload_length = get_db_payload(lr_buffer, l, tcp_buffer, &seq_num_proxy, &radiotap_length);
                        if (seq_num_proxy != last_recv_seq_num) {
                            last_recv_seq_num = seq_num_proxy;
                            log_telem_to_file(log_file.file_pntr, tcp_buffer, payload_length);
                            send_to_all_tcp_clients(tcp_clients, tcp_buffer, payload_length);
                            if (fifo_osd != -1 && write_to_osdfifo == 'Y') {
                                ssize_t written = write(fifo_osd, tcp_buffer, payload_length);
                                if (written < 1)
                                    perror("DB_PROXY_GROUND: Could not write to OSD FIFO");
                            }
                        }
                    } else
                        LOG_SYS_STD(LOG_ERR, "DB_PROXY_GROUND: Long range socket received an error: %s\n", strerror(err));
                }
            }
            // handle incoming tcp connection requests on master TCP socket
            if (FD_ISSET(tcp_server_info.sock_fd, &fd_socket_set)) {
                if ((new_tcp_client = accept(tcp_server_info.sock_fd,
                                             (struct sockaddr *) &tcp_server_info.servaddr,
                                             (socklen_t *) &tcp_addrlen)) < 0) {
                    perror("DB_PROXY_GROUND: Accepting new tcp connection failed");
                }
                LOG_SYS_STD(LOG_INFO, "DB_PROXY_GROUND: New connection (%s:%d)\n", inet_ntoa(tcp_server_info.servaddr.sin_addr),
                       ntohs(tcp_server_info.servaddr.sin_port));
                //add new socket to array of sockets
                for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
                    if (tcp_clients[i] == 0) {   // if position is empty
                        tcp_clients[i] = new_tcp_client;
                        break;
                    }
                }
            }
            // handle messages from connected TCP clients
            for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
                int current_client_sock = tcp_clients[i];
                if (FD_ISSET(current_client_sock, &fd_socket_set)) {
                    if ((recv_length = read(current_client_sock, tcp_buffer, TCP_BUFFER_SIZE)) == 0) {
                        //Somebody disconnected , get his details and print
                        getpeername(current_client_sock, (struct sockaddr *) &tcp_server_info.servaddr,
                                    (socklen_t *) &tcp_addrlen);
                        LOG_SYS_STD(LOG_INFO, "DB_PROXY_GROUND: Client disconnected (%s:%d)\n",
                               inet_ntoa(tcp_server_info.servaddr.sin_addr),
                               ntohs(tcp_server_info.servaddr.sin_port));
                        close(current_client_sock);
                        tcp_clients[i] = 0;
                    } else {
                        // client sent us some information. Process it...
                        memcpy(data_uni_to_drone->bytes, tcp_buffer, recv_length);
                        for (int j = 0; j < num_interfaces; j++)
                            db_send_hp_div(&raw_interfaces[j], DB_PORT_CONTROLLER, (u_int16_t) recv_length,
                                           update_seq_num(&seq_num));
                    }
                }
            }
        }
    }
    for (int i = 0; i < DB_MAX_ADAPTERS; i++) {
        if (raw_interfaces[i].db_socket > 0)
            close(raw_interfaces[i].db_socket);
    }
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
        if (tcp_clients[i] > 0)
            close(tcp_clients[i]);
    }
    close(tcp_server_info.sock_fd);
    if (fifo_osd > 0)
        close(fifo_osd);
    if (log_file.file_pntr != NULL) {
        fflush(log_file.file_pntr);
        fclose(log_file.file_pntr);
        // delete log file if empty
        struct stat st;
        stat(log_file.file_name, &st);
        if (st.st_size <= 1)
            remove(log_file.file_name);
    }
    LOG_SYS_STD(LOG_INFO, "DB_PROXY_GROUND: Terminated\n");
    exit(0);
}