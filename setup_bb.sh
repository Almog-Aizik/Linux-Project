#!/bin/bash

EXEC_PATH="$PWD/bb-daemon"

sudo bash -c "cat <<SERVICE > /etc/systemd/system/i2c-daemon.service
[Unit]
Description=BeagleBone i2c + ethernet Services

[Service]
ExecStart=$EXEC_PATH
WorkingDirectory=$PWD
Restart=always
RestartSec=3s

[Install]
WantedBy=multi-user.target
SERVICE"

sudo systemctl daemon-reload
sudo systemctl enable --now i2c-daemon.service
