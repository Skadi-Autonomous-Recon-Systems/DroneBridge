/*
 *   This file is part of DroneBridge: https://github.com/seeul8er/DroneBridge
 *
 *   Copyright 2017 Wolfgang Christl
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

#ifndef CONTROL_TX_H
#define CONTROL_TX_H

#include "../common/db_protocol.h"

int send_rc_packet(uint16_t channel_data[]);

void get_joy_interface_path(char *dst_joy_interface_path, int joy_interface_indx);

void do_calibration(char *calibrate_comm, int joy_interface_indx);

void conf_rc(char adapters[DB_MAX_ADAPTERS][IFNAMSIZ], int num_inf_rc, int comm_id, char db_mode, int bitrate_op,
            int frame_type, int new_rc_protocol, char allow_rc_overwrite, int adhere_80211);

void open_rc_shm();

void close_raw_interfaces();

#endif //CONTROL_TX_H
