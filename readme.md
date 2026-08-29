## setup

upload the bb-daemon and the setup_bb.sh script to the beaglebone, save them both to the desired location (they need to share a folder) and run the script.
the script will set up the daemon and start it automatically.

a config file will be generated alongside the binary, a restart of the daemon is needed to load the updated config file.
for the server, run the admin panel first, config file should be generated on first run with the location for the server binary. keep in mind that any changes to the config file require a restart to apply.

start the server from the admin panel, it'll generate its own config file. keep in mind that any changes to the config file require a restart to apply.

## usage instructions

config files for each component exists, to modify stuffl like port and IP.
as for controlling the actual program, admin panel includes the following:

1. query database
   access the database and do one of the following: - get_city_info
   – list all available cities and lets you get information about a specific one - get_trans
   – get the transactions made - get_log
   – read the server log
2. update or add entry – asks you for coordinations and based on whether a city exists there or not, either add a city or update its price
3. delete entry - deletes a city based on its coordinates
4. import from file - name dynamically updates based on the config file, imports cities from the file
5. start server – starts the server binary, location taken from the config file
6. stop server - stops the server process

#### import file format

config file should be formattted as such:

`<name>, <X coordinate>, <Y coordinate>, <price>`

malformed lines are skipped.

supplied a cities.txt as an example.

## build instructions

requirements:

- gcc
- arm-gnu-toolchain

- stm32 IDE

to build, go to the main folder (not any of the sub-folders).

`build all` - make all binaries needed excluding the STM32

`build server` `build admin_panel` `build BB` - builds individual components

for `STM32 RNG` needs to be loaded to an STM32 IDE and bult specifically for the card you're using.

the supplied project is for nucleo L432KC but can be converted to other cards with the CubeMX importer.
