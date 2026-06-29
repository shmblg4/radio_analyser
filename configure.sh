usbipd.exe attach --wsl --busid 3-3 && usbipd.exe attach --wsl --busid 3-2 && sleep 1 && 
sudo rmmod ftdi_sio && sleep 1 && lsusb && lsusb -t
