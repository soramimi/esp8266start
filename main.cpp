
#include "serial.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>

#ifdef WIN32
#pragma warning(disable:4996)
#define COMPORT "\\\\.\\COM1"
#else
//#define COMPORT "/dev/ttyS0"
#define COMPORT "/dev/ttyUSB0"
#endif

void esp_send_command(serial_handle_t handle, std::string str)
{
	str += "\r\n";
	serial_write(handle, str.c_str(), str.size());
}

void esp_recv_response(serial_handle_t handle, std::vector<std::string> *lines)
{
	lines->clear();
	std::vector<char> vec;
	char tmp[1024];
	int i = 0;
	int offset = 0;
	int length = 0;
	vec.reserve(1024);
	while (1) {
		if (offset < length) {
			goto L1;
		}
		offset = 0;
		for (int i = 0; i < 10; i++) {
			length = serial_read(handle, tmp, sizeof(tmp), 20);
			if (length > 0) {
				goto L1;
			}
		}
		break;
L1:;
		int c = tmp[offset] & 0xff;
		offset++;
		if (c == 0x0a) {
			if (!vec.empty() && vec.back() == 0x0d) {
				char const *p = &vec[0];
				int n = vec.size();
				while (n > 0 && isspace(vec[n - 1] & 0xff)) n--;
				std::string s(p, p + n);
				lines->push_back(s);
				if (s == "OK") break;
				vec.clear();
			}
		} else {
			vec.push_back(c);
		}
	}
}

void print_lines(std::vector<std::string> const *lines)
{
	for (std::string line : *lines) {
		puts(line.c_str());
	}
}

bool esp_run_command_(serial_handle_t handle, std::string const &command, std::vector<std::string> *lines)
{
	lines->clear();
	esp_send_command(handle, command);
	esp_recv_response(handle, lines);
	if (lines->size() >= 2 && lines->front() == command && lines->back() == "OK") {
		lines->pop_back();
		lines->erase(lines->begin());
		return true;
	}
	return false;
}

bool esp_run_command(serial_handle_t handle, std::string const &command, std::vector<std::string> *lines, int retry = 3)
{
	while (retry > 0) {
		if (esp_run_command_(handle, command, lines)) {
			return true;
		}
		serial_flush_input(handle, 100);
		retry--;
	}
	return false;
}

bool esp_run_command(serial_handle_t handle, std::string const &command, bool printresult)
{
	std::vector<std::string> lines;
	if (esp_run_command(handle, command, &lines, 3)) {
		if (printresult) {
			print_lines(&lines);
		}
		return true;
	}
	return false;
}

static std::string trim(char const **left, char const **right)
{
	char const *l = *left;
	char const *r = *right;
	while (l < r && isspace(*l & 0xff)) l++;
	while (l < r && isspace(r[-1] & 0xff)) r--;
	return std::string(l, r);
}

static std::string trim_quot(std::string const &str)
{
	char const *left = str.c_str();
	char const *right = left + str.size();
	char const *l = left;
	char const *r = right;
	if (l + 1 < r && *l == '\"' && r[-1] == '\"') {
		l++;
		r--;
	}
	if (l == left && r == right) return str;
	return std::string(l, r);
}

bool lookup(std::vector<std::string> *lines, std::string const &key, std::string *out)
{
	for (std::string const &line : *lines) {
		char const *begin = line.c_str();
		char const *end = begin + line.size();
		char const *p = strchr(begin, ':');
		if (p) {
			char const *left;
			char const *right;;
			left = begin;
			right = p;
			std::string k = trim(&left, &right);
			left = p + 1;
			right = end;
			std::string v = trim(&left, &right);
			if (k == key) {
				*out = v;
				return true;
			}
		}
	}
	out->clear();
	return false;
}

struct mac_address_t {
	uint8_t a = 0;
	uint8_t b = 0;
	uint8_t c = 0;
	uint8_t d = 0;
	uint8_t e = 0;
	uint8_t f = 0;
};

static std::string to_s(mac_address_t const &mac)
{
	char tmp[100];
	sprintf(tmp, "%02X:%02X:%02X:%02X:%02X:%02X", mac.a, mac.b, mac.c, mac.d, mac.e, mac.f);
	return tmp;
}

static bool esp_get_mac_address_(serial_handle_t handle, std::string const &cmd, std::string const &key, mac_address_t *out)
{
	*out = mac_address_t();
	std::vector<std::string> lines;
	if (esp_run_command(handle, cmd, &lines)) {
		std::string t;
		if (lookup(&lines, key, &t)) {
			t = trim_quot(t);
			int a, b, c, d, e, f;
			if (sscanf(t.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x", &a, &b, &c, &d, &e, &f) == 6) {
				out->a = a;
				out->b = b;
				out->c = c;
				out->d = d;
				out->e = e;
				out->f = f;
			}
			return true;
		}
	}
	return false;
}

bool esp_get_st_mac_address(serial_handle_t handle, mac_address_t *out)
{
	return esp_get_mac_address_(handle, "AT+CIPSTAMAC?", "+CIPSTAMAC", out);
}

bool esp_get_ap_mac_address(serial_handle_t handle, mac_address_t *out)
{
	return esp_get_mac_address_(handle, "AT+CIPAPMAC?", "+CIPAPMAC", out);
}

int main(int argc, char **argv)
{
	serial_handle_t handle = INVALID_HANDLE_VALUE;
	SerialOption opt;
	opt.port = COMPORT;
	opt.speed = 115200;
	if (serial_open(&opt, &handle)) {
		esp_run_command(handle, "AT+GMR", true);

		mac_address_t stmac;
		esp_get_st_mac_address(handle, &stmac);
		puts(("ST " + to_s(stmac)).c_str());

		mac_address_t apmac;
		esp_get_ap_mac_address(handle, &apmac);
		puts(("AP " + to_s(apmac)).c_str());

		serial_close(handle, &opt);
	}

	return 0;
}

