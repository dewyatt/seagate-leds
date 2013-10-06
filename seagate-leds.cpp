/**
* The MIT License (MIT)
*
* Copyright (c) 2013 Daniel Wyatt
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
**/
extern "C" {
	#include <scsi/sg_pt.h>
	#include <scsi/sg_cmds.h>
	#include <sysexits.h>
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <ctime>
#include <set>

const int MAX_SERIAL_LEN = 20;

using std::set;

struct ScsiCmd {
	uint8_t *data;
	uint8_t datalen;
	uint8_t cdb[16];
	uint8_t cdblen;
};

enum {
	VPD_SERIAL = 0x80,
	VPD_FEATURES = 0xC1,
	VPD_INTERFACES = 0xC2,
};

enum {
	FEATURE_POWER = 8,
	FEATURE_LED = 55,
	FEATURE_LED_CAPACITY = 56
};

const char *prog_name;

//Global inquiry buffer
uint8_t inqbuffer[255];

struct Device {
	const char *dev_name;
	char vendor_id[9];
	char product_id[17];
	char revision[5];
	uint16_t vid;
	uint16_t pid;
	set<uint8_t> supported_vpds;
	set<uint8_t> features;
	set<uint8_t> interfaces;
	char serial[MAX_SERIAL_LEN + 1];
};

void show_usage () {
	fprintf ( stderr, "Usage: %s <device> <info|led|capacity-led> [value]\n", prog_name );
	fprintf ( stderr, "Examples:\n" );
	fprintf ( stderr, "\t%s /dev/disk/by-label/goflex info\n", prog_name);
	fprintf ( stderr, "\n" );
	fprintf ( stderr, "\t%s /dev/disk/by-label/goflex led\n", prog_name);
	fprintf ( stderr, "\t%s /dev/disk/by-label/goflex led on\n", prog_name);
	fprintf ( stderr, "\n" );
	fprintf ( stderr, "\t%s /dev/disk/by-label/goflex capacity-led 15\n", prog_name );
	fprintf ( stderr, "\t%s /dev/disk/by-label/goflex capacity-led 90%%\n", prog_name );
}

void trim ( char *s, size_t length ) {
	char *start = s;
	char *end = s + length - 1;
	while ( start <= end && isspace ( *start ) )
		start++;

	while ( end >= start && isspace ( *end ) )
		end--;

	*(end + 1) = '\0';
	memmove ( s, start, end - start + 2 );
}

bool spt_send_cmd_out ( int sg_fd, ScsiCmd &cmd ) {
	sg_pt_base *pt = 0;
	int ret;

	pt = ( sg_pt_base* ) ( intptr_t ) construct_scsi_pt_obj();
	if ( !pt ) {
		fprintf ( stderr, "construct_scsi_pt_obj() failed" );
		return false;
	}
	set_scsi_pt_cdb ( pt, cmd.cdb, cmd.cdblen );
	set_scsi_pt_data_out ( pt, cmd.data, cmd.datalen );
	ret = do_scsi_pt ( pt, sg_fd, 6, 0 );
	if ( 0 != ret ) {
		fprintf ( stderr, "do_scsi_pt() failed [%d]\n", ret );
		destruct_scsi_pt_obj( pt );
		return false;
	}
	destruct_scsi_pt_obj( pt );
	return true;
}

bool spt_send_cmd_in ( int sg_fd, ScsiCmd &cmd ) {
	sg_pt_base *pt = 0;
	int ret;

	pt = ( sg_pt_base* ) ( intptr_t ) construct_scsi_pt_obj();
	if ( !pt ) {
		fprintf ( stderr, "construct_scsi_pt_obj() failed" );
		return false;
	}
	set_scsi_pt_cdb ( pt, cmd.cdb, cmd.cdblen );
	set_scsi_pt_data_in ( pt, cmd.data, cmd.datalen );
	ret = do_scsi_pt ( pt, sg_fd, 10, 0 );
	if ( 0 != ret ) {
		fprintf ( stderr, "do_scsi_pt() failed [%d]\n", ret );
		destruct_scsi_pt_obj( pt );
		return false;
	}
	destruct_scsi_pt_obj( pt );
	return true;
}

bool scsiop_getled ( int sg_fd, uint8_t *value ) {
	ScsiCmd cmd;
	cmd.data = value;
	cmd.datalen = 4;
	cmd.cdblen = 10;
	
	memset ( cmd.cdb, 0, 10 );
	cmd.cdb[0] = 0xFA;
	cmd.cdb[6] = 4;
	return spt_send_cmd_in ( sg_fd, cmd );
}

bool scsiop_setled ( int sg_fd, uint8_t *value ) {
	ScsiCmd cmd;
	cmd.data = value;
	cmd.datalen = 4;
	cmd.cdblen = 10;

	memset ( cmd.cdb, 0, 10 );
	cmd.cdb[0] = 0xF9;
	cmd.cdb[6] = 4;
	return spt_send_cmd_out ( sg_fd, cmd );
}

bool scsiop_getcapacityled ( int sg_fd, uint8_t *value ) {
	ScsiCmd cmd;
	cmd.data = value;
	cmd.datalen = 4;
	cmd.cdblen = 10;
	
	memset ( cmd.cdb, 0, 10 );
	cmd.cdb[0] = 0xF7;
	cmd.cdb[6] = 4;
	return spt_send_cmd_in ( sg_fd, cmd );
}

bool scsiop_setcapacityled ( int sg_fd, uint8_t *value ) {
	ScsiCmd cmd;
	cmd.data = value;
	cmd.datalen = 4;
	cmd.cdblen = 10;
	
	memset ( cmd.cdb, 0, 10 );
	cmd.cdb[0] = 0xF8;
	cmd.cdb[6] = 4;
	return spt_send_cmd_out ( sg_fd, cmd );
}

bool get_led_state ( int sg_fd, bool &led_on, bool &led1, bool &led2 ) {
	uint8_t state[4];
	if ( !scsiop_getled ( sg_fd, state ) )
		return false;

	if ( state[0] == 1 || state[1] == 1 )
		led_on = false;
	if ( state[0] == 2 || state[1] == 2 )
		led_on = true;

	led1 = state[0] != 0xFF;
	led2 = state[1] != 0xFF;
	return true;
}

bool get_led ( int sg_fd, bool &led_on ) {
	bool led1, led2;
	return get_led_state ( sg_fd, led_on, led1, led2 );
}

bool set_led_state ( int sg_fd, bool led_on, bool led1, bool led2 ) {
	uint8_t state[4];
	memset ( state, 0, 4 );
	if (led_on) {
		state[0] = 2;
		state[1] = led2 ? 3 : 0;
	} else {
		state[0] = (led1 ? 2 : 0) - 1;
		state[1] = led2 ? 2 : 0;
	}
	state[1] = state[1] - 1;
	return scsiop_setled ( sg_fd, state );
}

bool set_led ( int sg_fd, bool led_on ) {
	bool unused;
	bool led1, led2;
	if ( !get_led_state ( sg_fd, unused, led1, led2 ) )
		return false;

	return set_led_state ( sg_fd, led_on, led1, led2 );
}

bool get_capacity_led_state ( int sg_fd, uint8_t &state ) {
	uint8_t buffer[4] = {0};
	if ( !scsiop_getcapacityled ( sg_fd, buffer ) )
		return false;

	state = buffer[1];
	return true;
}

bool set_capacity_led_state ( int sg_fd, uint8_t state ) {
	uint8_t buffer[4] = {4, state, 0, 0};
	return scsiop_setcapacityled ( sg_fd, buffer );
}

uint8_t get_capacity_percent_flags ( uint8_t percent ) {
	if ( percent <= 25 )
		return 1; //0001
	else if ( percent <= 50 )
		return 3; //0011
	else if ( percent <= 85 )
		return 7; //0111

	return 15; //1111
}

bool set_capacity_led_percent ( int sg_fd, bool led_on, uint8_t percent ) {
	if ( percent > 100 )
		return false;

	uint8_t unused;
	get_capacity_led_state ( sg_fd, unused );
	uint8_t state[4] = {4, 0, 0, 0};
	if ( led_on ) {
		uint8_t flags = get_capacity_percent_flags ( percent );
		state[1] = flags;
	} else {
		state[1] = 0;
	}
	return scsiop_setcapacityled ( sg_fd, state );
}

const char *get_vpd_description ( uint8_t page ) {
	switch ( page ) {
		case 0:
			return "Standard Inquiry";
		case 0x80:
			return "Serial Number";
		case 0x83:
			return "Device Identification";
		case 0xC0:
			return "Password Security Status";
		case 0xC1:
			return "Features";
		case 0xC2:
			return "Interfaces";
		case 0xC3:
			return "Button Status";
		case 0xC4:
			return "RAID Configuration";
		case 0xC5:
			return "Tattoo Status";
		case 0xC6:
			return "RAID Status";
	}
	return "Unknown VPD Page";
}

const char *get_feature_description ( uint8_t feature ) {
	switch ( feature ) {
		case 1:
			return "Button";
		case 8:
			return "Power";
		case 9:
			return "Acoustic";
		case 11:
			return "Discrete Storage";
		case 12:
			return "Security";
		case 16:
			return "Raid 0 Only";
		case 17:
			return "Raid 0/1";
		case 20:
			return "ATA Pass Through";
		case 21:
			return "SMART using LOG SENSE/SELECT";
		case 24:
			return "Prolific Firmware";
		case 25:
			return "Cypress FW Downloader";
		case 28:
			return "Small Form Factor 2.5";
		case 29:
			return "Mini Form Factor 1.8";
		case 30:
			return "Micro Form Factor 1.0";
		case 32:
			return "USB Power Supported"; //USE Power Supported => typo?
		case 36:
			return "FDE";
		case 40:
			return "Tattoo Display";
		case 44:
			return "Removable Cartridge";
		case 48:
			return "T10 SAT Diag";
		case 49:
			return "T10 SAT";
		case 50:
			return "T10 SAT Limited SMART";
		case 55:
			return "LED Control";
		case 56:
			return "LED Capacity Control";
	}
	return "Unknown Feature";
}

const char *get_interface_description ( uint8_t interface ) {
	switch ( interface ) {
		case 1:
			return "1394A";
		case 2:
			return "1394B";
		case 4:
			return "USB";
		case 5:
			return "eSATA1.5Gb";
		case 6:
			return "eSATA3.0Gb";
		case 8:
			return "USBMiniB";
		case 9:
			return "eSATA6.0Gb";
		case 10:
			return "USB3.0";
	}
	return "Unknown Interface";
}

bool get_supported_vpds ( int sg_fd, set<uint8_t> &vpds ) {
	int ret = sg_ll_inquiry ( sg_fd, 0, 1, 0, inqbuffer, sizeof(inqbuffer), 0, 0 );
	if ( 0 != ret ) {
		fprintf ( stderr, "sg_ll_inquiry failed [%d]\n", ret);
		return false;
	}
	vpds.clear ();
	for ( int i = 0; i < inqbuffer[3]; i++ )
		vpds.insert ( inqbuffer[4 + i] );

	return true;
}

bool get_std_info ( int sg_fd, Device &dev ) {
	int ret = sg_ll_inquiry ( sg_fd, 0, 0, 0, inqbuffer, sizeof(inqbuffer), 0, 0 );
	if ( 0 != ret ) {
		fprintf ( stderr, "sg_ll_inquiry failed [%d]\n", ret );
		return false;
	}
	memcpy ( dev.vendor_id, &inqbuffer[8], 8 );
	memcpy ( dev.product_id, &inqbuffer[16], 16 );
	memcpy ( dev.revision, &inqbuffer[32], 4 );
	if ( strncmp ( "Seagate ", dev.vendor_id, 8 ) != 0 &&
		 strncmp ( "Maxtor ", dev.vendor_id, 8 ) != 0 &&
		 strncmp ( "Seagate_", dev.vendor_id, 8 ) != 0 &&
		 strncmp ( "Maxtor_ ", dev.vendor_id, 8 ) != 0 ) {
		fprintf ( stderr, "This does not appear to be a Seagate/Maxtor device\n" );
		return false;
	}
	trim ( dev.vendor_id, 8 );
	trim ( dev.product_id, 16 );
	trim ( dev.revision, 4 );
	dev.pid = inqbuffer[36] * 0x100 + inqbuffer[37];
	dev.vid = inqbuffer[38] * 0x100 + inqbuffer[39];
	return true;
}

bool get_serial_number ( int sg_fd, char serial[] ) {
	int ret = sg_ll_inquiry ( sg_fd, 0, 1, VPD_SERIAL, inqbuffer, sizeof(inqbuffer), 0, 0 );
	if ( 0 != ret ) {
		fprintf ( stderr, "sg_ll_inquiry failed [%d]\n", ret);
		return false;
	}
	uint8_t len = inqbuffer[3];
	if ( len > MAX_SERIAL_LEN ) {
		fprintf ( stderr, "Serial number too long\n" );
		return false;
	}
	memcpy ( serial, &inqbuffer[4], len );
	trim ( serial, len );
	return true;
}

//Serials are 8 characters long, alphanumeric, no I,O,U
char encode_serial_char ( char c ) {
	//A-M
	if ( ( c >= 'A' && c <= 'M' ) || ( c >= 'a' && c <= 'm' ) )
		return c + 13;
	//N-Z
	else if ( ( c >= 'N' && c <= 'Z' ) || ( c >= 'n' && c <= 'z' ) )
		return c - 13;
	//0-4
	else if ( ( c >= '0' && c <= '4' ) )
		return c + 5;
	//5-9
	else if ( c >= '5' && c <= '9' )
		return c - 5;

	return c;
}

const char *gen_fryqrp ( const char *serial ) {
	static char buffer[32] = {0};
	char yearbuffer[5];
	int year;
	strncpy ( buffer, "DLFNDR", sizeof ( buffer ) );
	strncat ( buffer, serial, sizeof ( buffer ) );
	time_t t = time (NULL);
	struct tm *tm = localtime ( &t );
	year = tm->tm_year;
	snprintf ( yearbuffer, sizeof ( yearbuffer ), "%d", year + 1900 );
	strncat ( buffer, yearbuffer, sizeof ( buffer ) );
	for ( size_t i = 0; i < strlen ( buffer ); i++ )
		buffer[i] = encode_serial_char ( buffer[i] );

	return buffer;
}

const char *get_dlfndr_url ( const char *serial ) {
	static char buffer[128] =  "https://apps1.seagate.com/downloads/request.html?userPreferredLocaleCookie=en_EN_&fryqrp=";
	strncat ( buffer, gen_fryqrp ( serial ), sizeof ( buffer ) );
	return buffer;
}

bool get_features ( int sg_fd, set<uint8_t> &features ) {
	int ret = sg_ll_inquiry ( sg_fd, 0, 1, VPD_FEATURES, inqbuffer, sizeof(inqbuffer), 0, 0 );
	if ( 0 != ret ) {
		fprintf ( stderr, "sg_ll_inquiry failed [%d]\n", ret);
		return 1;
	}
	features.clear ();
	for ( int i = 0; i < inqbuffer[3]; i++ )
		features.insert ( inqbuffer[4 + i] );

	return true;
}

bool get_interfaces ( int sg_fd, set<uint8_t> &interfaces ) {
	int ret = sg_ll_inquiry ( sg_fd, 0, 1, VPD_INTERFACES, inqbuffer, sizeof(inqbuffer), 0, 0 );
	if ( 0 != ret ) {
		fprintf ( stderr, "sg_ll_inquiry failed [%d]\n", ret);
		return 1;
	}
	interfaces.clear ();
	for ( int i = 0; i < inqbuffer[3]; i++ )
		interfaces.insert ( inqbuffer[4 + i] );

	return true;
}

int cmd_info ( int sg_fd ) {
	Device dev;
	if ( !get_std_info ( sg_fd, dev ) )
		return 1;

	printf ( "%04X:%04X %s %s (%s)\n", dev.vid, dev.pid, dev.vendor_id, dev.product_id, dev.revision );
	if ( !get_supported_vpds ( sg_fd, dev.supported_vpds ) )
		return 1;

	printf ( "Supported VPD Pages:\n" );
	for ( set<uint8_t>::iterator it ( dev.supported_vpds.begin() ); it != dev.supported_vpds.end (); ++it )
		printf ("\t[0x%02X] %s\n", *it, get_vpd_description ( *it ) );

	if ( dev.supported_vpds.count ( VPD_SERIAL ) ) {
		if ( !get_serial_number ( sg_fd, dev.serial ) )
			return false;

		printf ( "Serial #: %s\n", dev.serial );
		printf ( "Download Finder URL: %s\n", get_dlfndr_url ( dev.serial ) );
	}
	if ( dev.supported_vpds.count ( VPD_FEATURES ) ) {
		if ( !get_features ( sg_fd, dev.features ) )
			return false;

		printf ( "Features:\n" );
		for ( set<uint8_t>::iterator it ( dev.features.begin () ); it != dev.features.end (); ++it )
			printf ( "\t[0x%02X] %s\n", *it, get_feature_description ( *it ) );
	}
	if ( dev.supported_vpds.count ( VPD_INTERFACES ) ) {
		if ( !get_interfaces ( sg_fd, dev.interfaces ) )
			return false;

		printf ( "Interfaces:\n" );
		for ( set<uint8_t>::iterator it ( dev.interfaces.begin () ); it != dev.interfaces.end (); ++it )
			printf ( "\t[0x%02X] %s%s\n", ( *it & 0xF ), get_interface_description ( *it & 0xF ), ( *it & 0x40 ) ? " [active]" : "" );
	}
	return 0;
}

int cmd_led_read ( int sg_fd ) {
	bool led;
	if ( !get_led ( sg_fd, led ) ) {
		fprintf ( stderr, "GetLed failed\n" );
		return 1;
	}
	printf ( "led: %s\n", led ? "on" : "off" );
	return 0;
}

int cmd_led_write ( int sg_fd, char *value ) {
	bool led;
	if ( strcmp ( value, "1" ) == 0 )
		led = true;
	else if ( strcmp ( value, "0" ) == 0 )
		led = false;
	else if ( strcmp ( value , "on" ) == 0 )
		led = true;
	else if ( strcmp ( value, "off" ) == 0 )
		led = false;
	else {
		fprintf ( stderr, "Invalid usage. Argument '%s' should be one of: 1, 0, on, off\n\n", value );
		show_usage ();
		return EX_USAGE;
	}
	if ( !set_led ( sg_fd, led ) )
		return false;

	return 0;
}

int cmd_cap_led_read ( int sg_fd ) {
	uint8_t state;
	if ( !get_capacity_led_state ( sg_fd, state ) )
		return 1;

	for ( int i = 0; i < 4; i++ ) {
		if ( state & 1 )
			printf ( "1" );
		else
			printf ( "0" );
			
		state >>= 1;
	}
	printf ( "\n" );
	return 0;
}

int cmd_cap_led_write ( int sg_fd, char *value ) {
	int len = strlen ( value );
	uint8_t flags = 0;
	if ( value[len - 1] == '%' ) {
		value[len - 1] = '\0';
		int percent = strtoul ( value, NULL, 10 );
		if ( percent > 100 ) {
			fprintf ( stderr, "Expected percentage value ('%s')\n\n", value );
			show_usage ();
			return EX_USAGE;
		}
		flags = get_capacity_percent_flags ( percent );
	} else if ( len == 4 ) {
		for ( int i = 0; i < 4; i++ ) {
			if ( value[i] != '0' && value[i] != '1' ) {
				fprintf ( stderr, "Expected binary argument ('%s')\n\n", value );
				show_usage ();
				return EX_USAGE;
			}
		}
		flags = strtoul ( value, NULL, 2 );
		flags = ( ( flags & 1 ) << 3 ) | ( ( flags & 2 ) << 1 ) | ( ( flags & 4 ) >> 1 ) | ( ( flags & 8 ) >> 3 );
	} else {
		flags = strtoul ( value, NULL, 10 );
	}
	if ( !set_capacity_led_state ( sg_fd, flags ) )
		return 1;

	return 0;
}

int handle_cmd ( int sg_fd, int argc, char *cmd, char *value ) {
	int ret = EX_OK;
	if ( strcmp ( cmd, "info" ) == 0 && argc == 3 ) {
		ret = cmd_info ( sg_fd );
	} else if ( strcmp ( cmd, "led" ) == 0 ) {
		if ( argc == 3 )
			ret = cmd_led_read ( sg_fd );
		else
			ret = cmd_led_write ( sg_fd, value );
	} else if ( strcmp ( cmd, "capacity-led" ) == 0 ) {
		if ( argc == 3 )
			ret = cmd_cap_led_read ( sg_fd );
		else
			ret = cmd_cap_led_write ( sg_fd, value );
	} else {
		fprintf ( stderr, "Incorrect usage\n\n" );
		show_usage();
		ret = EX_USAGE;
	}
	return ret;
}

int main ( int argc, char *argv[] ) {
	int sg_fd = -1;
	char *dev_name;
	char *cmd;
	char *value;
	int ret = EX_OK;

	prog_name = basename ( argv[0] );
	if ( argc != 3 && argc != 4 ) {
		show_usage();
		return EX_USAGE;
	}
	dev_name = argv[1];
	cmd = argv[2];
	value = argv[3];
	sg_fd = scsi_pt_open_device ( dev_name, 1, 0 );
	if ( sg_fd >= 0 ) {
		ret = handle_cmd ( sg_fd, argc, cmd, value );
		scsi_pt_close_device ( sg_fd );
	} else {
		fprintf ( stderr, "scsi_pt_open_device failed [%d]\n", sg_fd );
		ret = EX_UNAVAILABLE;
	}
	return ret;
}
