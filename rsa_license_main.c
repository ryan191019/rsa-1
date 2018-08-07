#include <time.h>
#if defined(__linux__)
#include <unistd.h>
#include <getopt.h>
#include <libgen.h>
#else
#include "getopt.h"
#endif
#include "rsa_license.h"

#if defined(__linux__)
#define TIME_LIMIT_FMT "%lu"
#define GMTIME_R(_t_, _tm_) gmtime_r(_t_, _tm_)
#else
#define TIME_LIMIT_FMT "%llu"
#define GMTIME_R(_t_, _tm_) gmtime_s(_tm_, _t_)
#endif

#define FILE_FORMAT_VERSION 1
#define VENDOR_NAME_MAX_LENGTH 64
#define VENDOR_NAME_DEFAULT "Ilan Smith"
#define FILE_NAME_MAX_LENGTH 256

#define SECONDS_IN_HOUR (60 * 60)
#define SECONDS_IN_DAY (SECONDS_IN_HOUR * 24)
#define SECONDS_IN_WEEK (SECONDS_IN_DAY * 7)
#define SECONDS_IN_MONTH (SECONDS_IN_DAY * 30)
#define SECONDS_IN_YEAR (SECONDS_IN_DAY * 365)

#define OPT_FLAG(OPT) ((unsigned int)(1 << (OPT)))
#define OPT_ADD(flags, OPT) do { \
	if ((flags) & OPT_FLAG(OPT)) { \
		rsa_error_message(RSA_ERR_ARGREP); \
		return -1; \
	} \
	(flags) |= OPT_FLAG(OPT); \
} while (0)

#define OPT_ADD_ACTION(flags, OPT, action) do { \
	OPT_ADD(flags, OPT); \
	if (action) { \
		rsa_error_message(RSA_ERR_ARGCONFLICT); \
		return -1; \
	} \
	action = OPT_FLAG(OPT); \
} while (0)

#define OPT_FLAG_LIC_DATA(flags) ((flags) & (OPT_FLAG(RSA_OPT_LIC_VENDOR) | \
			OPT_FLAG(RSA_OPT_LIC_TIME_LIMIT) | \
			OPT_FLAG(RSA_OPT_LIC_TIME_UNIT)))

#define TU_VAL_DAY "day"
#define TU_VAL_WEEK "week"
#define TU_VAL_MONTH "month"
#define TU_VAL_YEAR "year"

#define ROUND_UP(val, round) ((((val) + (round) - 1) / (round)) * (round))

typedef enum {
	/* actions */
	RSA_OPT_LIC_HELP,
	RSA_OPT_LIC_CREATE,
	RSA_OPT_LIC_INFO,
	RSA_OPT_LIC_TEST,

	/* license creation data */
	RSA_OPT_LIC_VENDOR,
	RSA_OPT_LIC_TIME_UNIT,
	RSA_OPT_LIC_TIME_LIMIT,

	/* files to use */
	RSA_OPT_LIC_KEY,
	RSA_OPT_LIC_LICENSE,
	RSA_OPT_MAX
} rsa_opt_t;

struct rsa_license_data {
	u64 version;
	char vendor_name[VENDOR_NAME_MAX_LENGTH];
	time_t time_limit;
};

#if !defined(__linux__)
static char *basename(char *path)
{
	char *ptr;

	for (ptr = path + strlen(path) - 1; ptr >= path && *ptr != '\\'; ptr--);
	return ++ptr;
}
#endif

static void usage(char *app)
{
	printf("Usage: %s [ACTION] [OPTIONS]\n", app);
	printf("\n");
	printf("Where possible actions are:\n");
	printf("\n");
	printf(C_HIGHLIGHT "  -c, --create=FILE_NAME" C_NORMAL "\n");
	printf("       Create a license file with the following options:\n");
	printf(C_HIGHLIGHT "       -k, --key=PRIVATE_KEY" C_NORMAL "\n");
	printf("            Private RSA key (required)\n");
	printf(C_HIGHLIGHT "       -v, --vendor=VENDOR_NAME" C_NORMAL "\n");
	printf("            Vendor being licensed (default: "
		VENDOR_NAME_DEFAULT ")\n");
	printf(C_HIGHLIGHT "       -t, --time-limit=TIME" C_NORMAL "\n");
	printf("            Validity duration from license creation time "
		"(default: unlimited)\n");
	printf("            The time units can be modified using the -u/--unit "
		"switch\n");
	printf(C_HIGHLIGHT "       -u, --unit=TIME_UNITS" C_NORMAL "\n");
	printf("            Units of time with which to set the time limit\n");
	printf("            Options are: \"" TU_VAL_DAY "\", \"" TU_VAL_WEEK
		"\", \"" TU_VAL_MONTH "\" (default) and \"" TU_VAL_YEAR "\"\n");
	printf(C_HIGHLIGHT "  -i, --info=FILE_NAME.lic" C_NORMAL "\n");
	printf("       Extract license information with possible option:\n");
	printf(C_HIGHLIGHT "       -k, --key=PUBLIC_KEY" C_NORMAL "\n");
	printf("            Public RSA key (optional, default is embeded)\n");
	printf("\n");
	printf(C_HIGHLIGHT "  -x, --test" C_NORMAL "\n");
	printf("       Run license test\n");
	printf("\n");
	printf(C_HIGHLIGHT "  -h, --help" C_NORMAL "\n");
	printf("       Print this information and exit\n");
}

static int is_str_prefix(char *s1, char *s2)
{
	return !strncmp(s1, s2, strlen(s1));
}

static int parse_args(int argc, char **argv, unsigned long *action,
		char key[FILE_NAME_MAX_LENGTH],
		char license[FILE_NAME_MAX_LENGTH],
		char vendor_name[VENDOR_NAME_MAX_LENGTH], time_t *time_limit)
{
	char *optstring = "hc:i:xk:v:u:t:";
	struct option longopts[] = {
		{
			.name = "help",
			.val = 'h',
			.has_arg = no_argument,
			.flag = NULL,
		},
		{
			.name = "create",
			.val = 'c',
			.has_arg = required_argument,
			.flag = NULL,
		},
		{
			.name = "info",
			.val = 'i',
			.has_arg = required_argument,
			.flag = NULL,
		},
		{
			.name = "test",
			.val = 'x',
			.has_arg = no_argument,
			.flag = NULL,
		},
		{
			.name = "key",
			.val = 'k',
			.has_arg = required_argument,
			.flag = NULL,
		},
		{
			.name = "vendor",
			.val = 'v',
			.has_arg = required_argument,
			.flag = NULL,
		},
		{
			.name = "unit",
			.val = 'u',
			.has_arg = required_argument,
			.flag = NULL,
		},
		{
			.name = "time-limit",
			.val = 't',
			.has_arg = required_argument,
			.flag = NULL,
		},
		{ 0, 0, 0, 0 }
	};
	unsigned long flags = 0;
	int opt;
	char *endptr;
	int time_limit_unit = SECONDS_IN_MONTH;
	int time_limit_multiple = 0;
	char *app = basename(argv[0]);

	*action = 0;
	memset(license, 0, FILE_NAME_MAX_LENGTH);
	memset(vendor_name, 0, VENDOR_NAME_MAX_LENGTH);
	*time_limit = 0;

	while ((opt = getopt_long(argc, argv, optstring, longopts, NULL)) !=
			-1) {
		switch (opt) {
		case 'h':
			OPT_ADD_ACTION(flags, RSA_OPT_LIC_HELP, *action);
			break;
		case 'c':
			OPT_ADD_ACTION(flags, RSA_OPT_LIC_CREATE, *action);
			snprintf(license, FILE_NAME_MAX_LENGTH, "%s.lic",
				optarg);
			break;
		case 'i':
			OPT_ADD_ACTION(flags, RSA_OPT_LIC_INFO, *action);
			snprintf(license, FILE_NAME_MAX_LENGTH, "%s", optarg);
			break;
		case 'x':
			OPT_ADD_ACTION(flags, RSA_OPT_LIC_TEST, *action);
			break;
		case 'k':
			OPT_ADD(flags, RSA_OPT_LIC_KEY);
			snprintf(key, FILE_NAME_MAX_LENGTH, "%s",
				optarg);
			break;
		case 'v':
			OPT_ADD(flags, RSA_OPT_LIC_VENDOR);
			snprintf(vendor_name, VENDOR_NAME_MAX_LENGTH, "%s",
				optarg);
			break;
		case 'u':
			OPT_ADD(flags, RSA_OPT_LIC_TIME_UNIT);
			if (is_str_prefix(optarg, TU_VAL_DAY)) {
				time_limit_unit = SECONDS_IN_DAY;
			} else if (is_str_prefix(optarg, TU_VAL_WEEK)) {
				time_limit_unit = SECONDS_IN_WEEK;
			} else if (is_str_prefix(optarg, TU_VAL_MONTH)) {
				time_limit_unit = SECONDS_IN_MONTH;
			} else if (is_str_prefix(optarg, TU_VAL_YEAR)) {
				time_limit_unit = SECONDS_IN_YEAR;
			} else {
				rsa_error_message(RSA_ERR_TIMUNIT, optarg);
				return -1;
			}
			break;
		case 't':
			OPT_ADD(flags, RSA_OPT_LIC_TIME_LIMIT);
			time_limit_multiple = strtol(optarg, &endptr, 10);
			if (*endptr) {
				rsa_error_message(RSA_ERR_ARGNAN, optarg);
				return -1;
			}
			break;
		default:
			usage(app);
			break;
		}
	}

	if (!*action) {
		usage(app);
		return -1;
	}
	if (flags & OPT_FLAG(RSA_OPT_LIC_HELP) &&
			(flags & ~OPT_FLAG(RSA_OPT_LIC_HELP))) {
		rsa_error_message(RSA_ERR_ARGCONFLICT);
		usage(app);
		return -1;
	}
	if (OPT_FLAG_LIC_DATA(flags) &&
			!(*action & (OPT_FLAG(RSA_OPT_LIC_CREATE)))) {
		rsa_error_message(RSA_ERR_ARGCONFLICT);
		usage(app);
		return -1;
	}

	if ((flags & OPT_FLAG(RSA_OPT_LIC_TIME_UNIT)) &&
			!(flags & OPT_FLAG(RSA_OPT_LIC_TIME_LIMIT))) {
		printf("Warning: no time limit set so -u/--unit option is "
			"ignored\n");
	}

	if (time_limit_multiple) {
		*time_limit = time(NULL) + SECONDS_IN_DAY +
			time_limit_multiple * time_limit_unit;
	}

	return 0;
}

static int rsa_encrypt_format_version(char **buf, size_t *len, u64 version)
{
	size_t new_len = *len + sizeof(u64);

	if (!(*buf = (char*)realloc(*buf, new_len)))
		return -1;

	memcpy(*buf + *len, &version, sizeof(u64));

	*len = new_len;
	return 0;
}

static int rsa_encrypt_vendor_name(char **buf, size_t *len, char *vendor_name)
{
	size_t new_len;
	size_t name_len;
	int i;

	name_len = strlen(vendor_name);
	if (VENDOR_NAME_MAX_LENGTH <= name_len)
		return -1;

	new_len = *len + VENDOR_NAME_MAX_LENGTH;
	if (!(*buf = (char*)realloc(*buf, new_len)))
		return -1;

	for (i = 0; i < VENDOR_NAME_MAX_LENGTH; i++)
		(*buf)[*len + i] = vendor_name[i % (name_len + 1)];

	*len = new_len;
	return 0;
}

static int rsa_encrypt_time_limit(char **buf, size_t *len, time_t time_limit)
{
	size_t new_len = *len + sizeof(time_t);

	if (!(*buf = (char*)realloc(*buf, new_len)))
		return -1;

	memcpy(*buf + *len, &time_limit, sizeof(time_t));

	*len = new_len;
	return 0;
}

/*
 * Specific license file format:
 *
 * +----------+----------------------------+
 * | Type     | Semantic                   |
 * +----------+----------------------------+
 * | u64      | file format version        |
 * | char[64] | vendor name                |
 * | time_t   | time limit                 |
 * +----------+----------------------------+
 */
static int rsa_license_create_rivermax(char **buf, size_t *len, void *data)
{
	char *_buf = NULL;
	size_t _len = 0;
	struct rsa_license_data *license_data = (struct rsa_license_data*)data;

	if (rsa_encrypt_format_version(&_buf, &_len, license_data->version)) {
		printf("failed to encrypt file format version: %llu\n",
			license_data->version);
		goto error;
	}

	if (rsa_encrypt_vendor_name(&_buf, &_len, license_data->vendor_name)) {
		printf("failed to encrypt vendor name: %s\n",
			license_data->vendor_name);
		goto error;
	}

	if (rsa_encrypt_time_limit(&_buf, &_len, license_data->time_limit)) {
		printf("failed to encrypt time limit: " TIME_LIMIT_FMT "\n",
			license_data->time_limit);
		goto error;
	}

	*buf = _buf;
	*len = _len;
	return 0;

error:
	free(_buf);
	return -1;
}

static int rsa_decrypt_version(char **buf, size_t *len, u64 *version_enc)
{
	if (*len < sizeof(u64))
		return -1;

	*version_enc = **((u64**)buf);

	*buf += sizeof(u64);
	*len -= sizeof(u64);
	return 0;
}

static int rsa_info_version(char **buf, size_t *len, u64 *version)
{
	u64 version_enc;

	if (rsa_decrypt_version(buf, len, &version_enc))
		return -1;

	printf("License format version: %llu\n", version_enc);
	*version = version_enc;
	return 0;
}

static int rsa_extract_version(char **buf, size_t *len,
		struct rsa_license_data *data)
{
	if (rsa_decrypt_version(buf, len, &data->version))
		return -1;

	return 0;
}

static int rsa_decrypt_vendor_name(char **buf, size_t *len,
		char vendor_name[VENDOR_NAME_MAX_LENGTH])
{
	int i;

	if (*len < VENDOR_NAME_MAX_LENGTH)
		return -1;

	for (i = 0; i < VENDOR_NAME_MAX_LENGTH; i++)
		vendor_name[i] = (*buf)[i];

	*buf += VENDOR_NAME_MAX_LENGTH;
	*len -= VENDOR_NAME_MAX_LENGTH;
	return 0;
}

static int rsa_info_vendor_name(char **buf, size_t *len)
{
	char vendor_name[VENDOR_NAME_MAX_LENGTH];

	if (rsa_decrypt_vendor_name(buf, len, vendor_name))
		return -1;

	printf("Vendor name: %s\n", vendor_name);
	return 0;
}

static int rsa_extract_vendor_name(char **buf, size_t *len,
		struct rsa_license_data *data)
{
	if (rsa_decrypt_vendor_name(buf, len, data->vendor_name))
		return -1;

	return 0;
}

static time_t round_up_end_of_day_localtime(time_t time_limit)
{
	time_t gmt_ts, eod_ts;
	struct tm time_info_gmt;
	time_t gmt_offset;

	if (!time_limit)
		return 0;

	GMTIME_R(&time_limit, &time_info_gmt);
	time_info_gmt.tm_isdst = -1;
	gmt_ts = mktime(&time_info_gmt);
	if ((int)gmt_ts == -1)
		return (time_t)-1;

	gmt_offset = time_limit - gmt_ts;
	eod_ts = ROUND_UP(time_limit + gmt_offset, SECONDS_IN_DAY) -
		gmt_offset - 1;

	return eod_ts;
}

static int rsa_decrypt_time_limit(char **buf, size_t *len, time_t *time_limit)
{
	time_t abs_ts;
	time_t eod_ts;

	if (*len < sizeof(time_t))
		return -1;

	abs_ts = **(time_t**)buf;
	eod_ts = round_up_end_of_day_localtime(abs_ts);
	if ((int)eod_ts == -1)
		return -1;

	*time_limit = eod_ts;

	*buf += sizeof(time_t);
	*len -= sizeof(time_t);
	return 0;
}

static char *time_t_to_str(time_t time_limit)
{
	static char stime[50];

	if (time_limit) {
		time_t last_day = time_limit;

		strftime(stime, sizeof(stime),"%d %b, %Y",
			localtime(&last_day));
	} else {
		snprintf(stime, sizeof(stime), "Unlimited");
	}

	return stime;
}

static int rsa_info_time_limit(char **buf, size_t *len)
{
	time_t time_limit;

	if (rsa_decrypt_time_limit(buf, len, &time_limit))
		return -1;

	printf("Valid through: %s\n", time_t_to_str(time_limit));
	return 0;
}

static int rsa_extract_time_limit(char **buf, size_t *len,
		struct rsa_license_data *data)
{
	if (rsa_decrypt_time_limit(buf, len, &data->time_limit))
		return -1;

	return 0;
}

static int rsa_license_info_parse_v1(char **buf, size_t *len)
{
	if (rsa_info_vendor_name(buf, len)) {
		printf("Could not extract vendor name\n");
		return -1;
	}

	if (rsa_info_time_limit(buf, len)) {
		printf("Could not extract time limit\n");
		return -1;
	}

	return 0;
}

static int rsa_license_info_rivermax(char *buf, size_t len)
{
	u64 version = 0;
	int ret;

	if (rsa_info_version(&buf, &len, &version)) {
		printf("Could not extract license format version\n");
		return -1;
	}

	switch (version)
	{
	case 1:
		ret = rsa_license_info_parse_v1(&buf, &len);
		break;
	default:
		if (!version) {
			printf("Error: No such license version\n");
		} if (FILE_FORMAT_VERSION < version) {
			printf("Error: license tool supports license format "
				"versions up to %d\n", FILE_FORMAT_VERSION);
		} else {
			printf("Error: unknown license version error\n");
		}

		ret = -1;
		break;
	}

	return ret;
}

static int rsa_license_extract_rivermax(char *buf, size_t len, void *data)
{
	struct rsa_license_data *lic_data = (struct rsa_license_data*)data;

	if (rsa_extract_version(&buf, &len, lic_data))
		return -1;

	if (rsa_extract_vendor_name(&buf, &len, lic_data))
		return -1;

	if (rsa_extract_time_limit(&buf, &len, lic_data))
		return -1;

	return 0;
}

static time_t get_time_limit(int do_limit)
{
	time_t t = 0;
	char stime[50];

	t = time(NULL);
	strftime(stime, sizeof(stime),"%d %b, %Y", localtime(&t));
	printf("current time: %s\n", stime);

	if (do_limit) {
		t += SECONDS_IN_MONTH * 1;
		t -= (t % SECONDS_IN_DAY);
		t += SECONDS_IN_DAY;
		strftime(stime, sizeof(stime),"%d %b, %Y", localtime(&t));
	} else {
		t = 0;
		snprintf(stime, sizeof(stime), "unlimited");
	}

	printf("setting time limit to: %s\n", stime);

	return t;
}

int license_test(void)
{
	static unsigned char private_key_test[] = {
		0x49, 0x41, 0x53, 0x52, 0x53, 0x41, 0x50, 0x86,
		0x6c, 0xa3, 0x4f, 0x58, 0xd4, 0xf9, 0xf0, 0x2e,
		0xdf, 0xb3, 0xd8, 0xce, 0x14, 0x1d, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
		0x00, 0x00, 0x71, 0x76, 0x56, 0x8e, 0xb0, 0x56,
		0xfd, 0x74, 0x58, 0x97, 0x86, 0x47, 0x50, 0x5c,
		0x21, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x8d, 0xe2,
		0x04, 0x5f, 0xdf, 0xaa, 0x9f, 0x80, 0x98, 0xf6,
		0x9d, 0xea, 0x92, 0xb6, 0x6b, 0x3e, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
		0x00, 0x00, 0x17, 0x4e, 0x69, 0xad, 0xbd, 0xd6,
		0x91, 0x2c, 0x60, 0xcc, 0xf4, 0x4b, 0x29, 0x72,
		0xe3, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x81, 0x0f,
		0xd4, 0x85, 0x5f, 0x48, 0x22, 0x7d, 0xec, 0xa3,
		0x39, 0xf0, 0xad, 0xb8, 0x7e, 0x41, 0x41, 0xdf,
		0x50, 0xbf, 0x60, 0x41, 0x67, 0xa4, 0x09, 0x90,
		0x88, 0x48, 0x43, 0xad, 0xf0, 0x45, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00,
		0x00, 0x00, 0xa9, 0xe0, 0x2d, 0x4e, 0x87, 0xa6,
		0x8d, 0x16, 0xd0, 0x36, 0xa1, 0x79, 0xea, 0xa6,
		0xe5, 0x77, 0xb9, 0x25, 0x4f, 0x4b, 0x7d, 0xfe,
		0x62, 0x53, 0x62, 0xb9, 0xa6, 0xb5, 0x19, 0xd8,
		0x49, 0x6b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x45, 0xbe,
		0x58, 0x06, 0xef, 0xdf, 0xa4, 0xed, 0x44, 0x16,
		0x58, 0xb2, 0xd9, 0x6f, 0x93, 0xb4, 0x8d, 0x35,
		0xbb, 0x76, 0x65, 0xe8, 0x3a, 0xce, 0x24, 0xc1,
		0x66, 0x65, 0x9c, 0x87, 0x7f, 0x4b, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00,
		0x00, 0x00, 0x77, 0x96, 0x69, 0x26, 0x1d, 0xd3,
		0xa4, 0x96, 0x2a, 0xc6, 0x8d, 0xbd, 0x79, 0x51,
		0x89, 0x05, 0x4e, 0x44, 0x48, 0x41, 0xdc, 0xf7,
		0x96, 0x20, 0xa6, 0xcd, 0x04, 0x8f, 0x3e, 0xa7,
		0xeb, 0x2d, 0x82, 0x14, 0xed, 0xd9, 0x6c, 0x6f,
		0x5f, 0xa5, 0x7e, 0x72, 0x81, 0x7a, 0x8d, 0xe3,
		0x35, 0xff, 0x0f, 0xb0, 0x78, 0x6a, 0xce, 0xc5,
		0xfc, 0x00, 0x15, 0x3c, 0xc8, 0xb5, 0x8e, 0xd0,
		0xea, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0xf3, 0xf3,
		0x63, 0x1f, 0xd3, 0x1f, 0x91, 0x82, 0x0e, 0x05,
		0x12, 0x76, 0xd3, 0xc4, 0x80, 0xce, 0x03, 0x39,
		0x78, 0x32, 0x6f, 0x61, 0xcb, 0xd3, 0xca, 0xfc,
		0xdf, 0x84, 0x3d, 0xc3, 0x00, 0x13, 0xd2, 0x6a,
		0x90, 0xab, 0x38, 0x85, 0xe5, 0x8f, 0x48, 0x14,
		0x0f, 0x1d, 0x28, 0x7b, 0xf4, 0x13, 0xe5, 0x30,
		0x90, 0x62, 0x3a, 0x3b, 0x12, 0x51, 0xb0, 0xfc,
		0x68, 0x0d, 0xac, 0xd0, 0x8f, 0x42, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00,
		0x00, 0x00, 0x4e, 0x82, 0xc5, 0x7d, 0x4a, 0xd1,
		0xe1, 0xa0, 0x13, 0x8f, 0xbd, 0x92, 0x9f, 0xc0,
		0x39, 0x7b, 0x51, 0xee, 0x30, 0xcf, 0x23, 0xcb,
		0xda, 0xac, 0x74, 0xef, 0x72, 0xef, 0x41, 0x3b,
		0xea, 0x9f, 0x00, 0x62, 0x83, 0x87, 0x0e, 0x5e,
		0x88, 0x2b, 0x3e, 0x24, 0x2f, 0x4e, 0x96, 0x9f,
		0x84, 0xee, 0x83, 0x1e, 0xec, 0x54, 0xff, 0x20,
		0x55, 0xea, 0xc5, 0x73, 0x99, 0xbb, 0xd8, 0x36,
		0xd9, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0xf7, 0x42,
		0x78, 0x3e, 0x65, 0xbd, 0x97, 0x07, 0x75, 0xd2,
		0x4f, 0xca, 0x8d, 0x80, 0x92, 0x52, 0x04, 0x44,
		0x6f, 0x39, 0x71, 0xb5, 0x52, 0xf5, 0x07, 0x8f,
		0xba, 0x20, 0x63, 0x0e, 0xe9, 0xd1, 0xcc, 0xe0,
		0x06, 0x97, 0x48, 0xbb, 0x21, 0x38, 0xf5, 0x18,
		0xa2, 0xf5, 0xb5, 0x16, 0x97, 0xdc, 0xea, 0x5b,
		0xea, 0xd7, 0x7b, 0x50, 0x1f, 0x60, 0xa9, 0xe7,
		0xd4, 0x43, 0xec, 0x5f, 0x84, 0x62, 0x8a, 0x94,
		0x2a, 0x3c, 0x52, 0x54, 0x79, 0xa5, 0x83, 0xf3,
		0x93, 0xae, 0xd1, 0x68, 0xc1, 0xa3, 0x71, 0x7e,
		0xbf, 0xf6, 0x43, 0xcf, 0x23, 0x51, 0x2f, 0x24,
		0x8b, 0x18, 0x87, 0x3e, 0x67, 0x72, 0xa1, 0xef,
		0x3d, 0xf9, 0x52, 0xc8, 0x12, 0x8a, 0x0d, 0xa3,
		0xdf, 0xdc, 0x5c, 0x1e, 0xc3, 0x29, 0x80, 0xb6,
		0x13, 0x58, 0xc1, 0xa3, 0xdc, 0x9f, 0x78, 0x51,
		0x1b, 0x15, 0x69, 0x18, 0xc0, 0x10, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00,
		0x00, 0x00, 0x65, 0xb5, 0x20, 0xd9, 0x11, 0x3f,
		0xb4, 0xc2, 0x22, 0x8f, 0x34, 0x9a, 0xd5, 0x97,
		0x6c, 0xcd, 0x89, 0x77, 0xa1, 0x84, 0x03, 0x58,
		0xff, 0xba, 0xe6, 0x00, 0x93, 0x92, 0xca, 0x2f,
		0xf0, 0x92, 0xd1, 0xcf, 0x2a, 0x24, 0x29, 0x7c,
		0x23, 0x30, 0x92, 0xd6, 0xcd, 0xc3, 0x49, 0x19,
		0xf0, 0x52, 0xd7, 0xa2, 0xbb, 0x19, 0x06, 0xbd,
		0x4d, 0xc8, 0x01, 0x46, 0x57, 0x6a, 0x2d, 0xcf,
		0x03, 0xac, 0x82, 0x41, 0x13, 0x97, 0x56, 0x92,
		0x10, 0xa5, 0x62, 0x65, 0xcb, 0x84, 0x51, 0xf6,
		0x4c, 0x76, 0xa4, 0x37, 0x41, 0xb9, 0x63, 0x38,
		0x6c, 0xf4, 0xe3, 0xc2, 0xbc, 0x3b, 0x2f, 0x4f,
		0xc1, 0x29, 0xba, 0x2a, 0xa4, 0x7b, 0xb7, 0xf5,
		0x42, 0x67, 0x06, 0xdd, 0x33, 0x3e, 0xeb, 0xc5,
		0x76, 0xe3, 0x61, 0x6b, 0xaf, 0x5b, 0x7c, 0xcb,
		0x69, 0xd6, 0xbf, 0x4b, 0x3c, 0x7a, 0xfa, 0x54,
		0xe5, 0x17, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x89, 0x7f,
		0x57, 0x38, 0xc5, 0x4a, 0xe1, 0x71, 0x84, 0xab,
		0xa7, 0xd6, 0x38, 0x7d, 0x30, 0xd4, 0x9d, 0x3a,
		0x81, 0xb7, 0x72, 0x88, 0x91, 0x5f, 0xe7, 0xc8,
		0xc5, 0xbd, 0x50, 0xf7, 0x4c, 0xac, 0x5f, 0x2c,
		0x99, 0xd3, 0x8b, 0x97, 0x08, 0x71, 0xdb, 0x6c,
		0x1c, 0xf1, 0x43, 0x53, 0x76, 0x37, 0xce, 0x26,
		0xed, 0xf7, 0x67, 0x24, 0x47, 0x5e, 0xb9, 0x5c,
		0xe9, 0xcd, 0x96, 0x86, 0xaf, 0x66, 0xa9, 0xa6,
		0x42, 0x38, 0xa2, 0xc6, 0x36, 0x97, 0xf5, 0x11,
		0xaf, 0x66, 0xd0, 0xce, 0x22, 0x69, 0x89, 0x01,
		0xc3, 0xc2, 0x68, 0x65, 0x41, 0xf4, 0x0e, 0x76,
		0xc5, 0x5a, 0x61, 0xaf, 0x73, 0xbc, 0xdc, 0x45,
		0x4e, 0x6c, 0x13, 0x53, 0x60, 0x00, 0xbb, 0x81,
		0x75, 0xa4, 0xbd, 0xb5, 0xad, 0x10, 0x6a, 0x38,
		0x0c, 0x04, 0xa7, 0x65, 0x72, 0x5e, 0x93, 0x2d,
		0x24, 0x13, 0x94, 0xe0, 0x05, 0x11, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00,
		0x00, 0x00,
	};
	static unsigned char public_key_test[] = {
		0x49, 0x41, 0x53, 0x52, 0x53, 0x41, 0x0c, 0xb3,
		0xaf, 0xe2, 0x0b, 0xc2, 0x7b, 0xfa, 0x37, 0x7c,
		0xa0, 0x34, 0x21, 0xa2, 0xb0, 0x34, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
		0x00, 0x00, 0x59, 0xaf, 0xa8, 0xf7, 0x59, 0xcd,
		0x54, 0x19, 0xce, 0x41, 0x32, 0x91, 0x03, 0x62,
		0xec, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x8d, 0xe2,
		0x04, 0x5f, 0xdf, 0xaa, 0x9f, 0x80, 0x98, 0xf6,
		0x9d, 0xea, 0x92, 0xb6, 0x6b, 0x3e, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
		0x00, 0x00, 0x17, 0x4e, 0x69, 0xad, 0xbd, 0xd6,
		0x91, 0x2c, 0x60, 0xcc, 0xf4, 0x4b, 0x29, 0x72,
		0xe3, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xd1, 0xd9,
		0x2c, 0xd9, 0xe5, 0x5b, 0x98, 0x6c, 0xd6, 0xc9,
		0x8a, 0x12, 0x0d, 0x6c, 0x39, 0x63, 0x1e, 0x0b,
		0x29, 0xeb, 0x44, 0x38, 0x48, 0xc3, 0x92, 0xe5,
		0xb8, 0x40, 0xb8, 0x3b, 0xbe, 0x02, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00,
		0x00, 0x00, 0xa9, 0xe0, 0x2d, 0x4e, 0x87, 0xa6,
		0x8d, 0x16, 0xd0, 0x36, 0xa1, 0x79, 0xea, 0xa6,
		0xe5, 0x77, 0xb9, 0x25, 0x4f, 0x4b, 0x7d, 0xfe,
		0x62, 0x53, 0x62, 0xb9, 0xa6, 0xb5, 0x19, 0xd8,
		0x49, 0x6b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x45, 0xbe,
		0x58, 0x06, 0xef, 0xdf, 0xa4, 0xed, 0x44, 0x16,
		0x58, 0xb2, 0xd9, 0x6f, 0x93, 0xb4, 0x8d, 0x35,
		0xbb, 0x76, 0x65, 0xe8, 0x3a, 0xce, 0x24, 0xc1,
		0x66, 0x65, 0x9c, 0x87, 0x7f, 0x4b, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00,
		0x00, 0x00, 0x0f, 0x79, 0x71, 0xf9, 0xe2, 0x9d,
		0x7b, 0xe0, 0xf5, 0xcc, 0xf6, 0x25, 0xdf, 0xdb,
		0x22, 0x87, 0xd7, 0x96, 0x3f, 0xe6, 0xe2, 0xb4,
		0x4d, 0x83, 0xeb, 0x0f, 0x66, 0xe3, 0x01, 0xee,
		0xb3, 0x33, 0x96, 0xc5, 0x71, 0x5a, 0x56, 0xe1,
		0x05, 0x00, 0xaa, 0xdb, 0xa4, 0x35, 0xaa, 0x03,
		0x0e, 0x3f, 0x50, 0x38, 0xc5, 0x51, 0x5c, 0x86,
		0xee, 0x15, 0x60, 0xab, 0x3a, 0xe7, 0xcf, 0x34,
		0x0b, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0xf3, 0xf3,
		0x63, 0x1f, 0xd3, 0x1f, 0x91, 0x82, 0x0e, 0x05,
		0x12, 0x76, 0xd3, 0xc4, 0x80, 0xce, 0x03, 0x39,
		0x78, 0x32, 0x6f, 0x61, 0xcb, 0xd3, 0xca, 0xfc,
		0xdf, 0x84, 0x3d, 0xc3, 0x00, 0x13, 0xd2, 0x6a,
		0x90, 0xab, 0x38, 0x85, 0xe5, 0x8f, 0x48, 0x14,
		0x0f, 0x1d, 0x28, 0x7b, 0xf4, 0x13, 0xe5, 0x30,
		0x90, 0x62, 0x3a, 0x3b, 0x12, 0x51, 0xb0, 0xfc,
		0x68, 0x0d, 0xac, 0xd0, 0x8f, 0x42, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00,
		0x00, 0x00, 0x4e, 0x82, 0xc5, 0x7d, 0x4a, 0xd1,
		0xe1, 0xa0, 0x13, 0x8f, 0xbd, 0x92, 0x9f, 0xc0,
		0x39, 0x7b, 0x51, 0xee, 0x30, 0xcf, 0x23, 0xcb,
		0xda, 0xac, 0x74, 0xef, 0x72, 0xef, 0x41, 0x3b,
		0xea, 0x9f, 0x00, 0x62, 0x83, 0x87, 0x0e, 0x5e,
		0x88, 0x2b, 0x3e, 0x24, 0x2f, 0x4e, 0x96, 0x9f,
		0x84, 0xee, 0x83, 0x1e, 0xec, 0x54, 0xff, 0x20,
		0x55, 0xea, 0xc5, 0x73, 0x99, 0xbb, 0xd8, 0x36,
		0xd9, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x23, 0xaa,
		0x49, 0xe8, 0xfe, 0xf2, 0x8d, 0x89, 0xb9, 0xe7,
		0xde, 0x65, 0xb4, 0x4b, 0x50, 0x77, 0x56, 0x32,
		0xf5, 0x12, 0x9a, 0x97, 0xa3, 0x16, 0x9e, 0x3d,
		0x29, 0xc7, 0xf1, 0x40, 0xee, 0xe1, 0xed, 0xb5,
		0x37, 0x55, 0x29, 0xb4, 0x0b, 0x9f, 0x3c, 0xeb,
		0x58, 0x60, 0x3a, 0xb2, 0xf0, 0x8e, 0x83, 0x18,
		0x2e, 0xea, 0xec, 0xd5, 0x62, 0xe6, 0xfd, 0xa4,
		0x30, 0xde, 0xf5, 0xcc, 0xf7, 0x65, 0x5f, 0xb3,
		0x41, 0x68, 0x68, 0x4b, 0x62, 0x4b, 0x17, 0x1e,
		0x67, 0xa0, 0xbf, 0xd2, 0xd5, 0x5b, 0x5e, 0x98,
		0x00, 0x72, 0xbf, 0xc1, 0x7b, 0xe3, 0xa5, 0xdf,
		0x79, 0xb8, 0x3d, 0x38, 0xfb, 0x5b, 0xcc, 0x62,
		0xcb, 0x17, 0x31, 0x9f, 0xe0, 0x2b, 0x5a, 0x7a,
		0x1e, 0xa5, 0x1b, 0xac, 0xe2, 0x1d, 0xac, 0xe0,
		0xb7, 0x78, 0x1a, 0x51, 0x7c, 0x68, 0xea, 0xc5,
		0x1e, 0x03, 0x35, 0xdc, 0x13, 0x02, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00,
		0x00, 0x00, 0x65, 0xb5, 0x20, 0xd9, 0x11, 0x3f,
		0xb4, 0xc2, 0x22, 0x8f, 0x34, 0x9a, 0xd5, 0x97,
		0x6c, 0xcd, 0x89, 0x77, 0xa1, 0x84, 0x03, 0x58,
		0xff, 0xba, 0xe6, 0x00, 0x93, 0x92, 0xca, 0x2f,
		0xf0, 0x92, 0xd1, 0xcf, 0x2a, 0x24, 0x29, 0x7c,
		0x23, 0x30, 0x92, 0xd6, 0xcd, 0xc3, 0x49, 0x19,
		0xf0, 0x52, 0xd7, 0xa2, 0xbb, 0x19, 0x06, 0xbd,
		0x4d, 0xc8, 0x01, 0x46, 0x57, 0x6a, 0x2d, 0xcf,
		0x03, 0xac, 0x82, 0x41, 0x13, 0x97, 0x56, 0x92,
		0x10, 0xa5, 0x62, 0x65, 0xcb, 0x84, 0x51, 0xf6,
		0x4c, 0x76, 0xa4, 0x37, 0x41, 0xb9, 0x63, 0x38,
		0x6c, 0xf4, 0xe3, 0xc2, 0xbc, 0x3b, 0x2f, 0x4f,
		0xc1, 0x29, 0xba, 0x2a, 0xa4, 0x7b, 0xb7, 0xf5,
		0x42, 0x67, 0x06, 0xdd, 0x33, 0x3e, 0xeb, 0xc5,
		0x76, 0xe3, 0x61, 0x6b, 0xaf, 0x5b, 0x7c, 0xcb,
		0x69, 0xd6, 0xbf, 0x4b, 0x3c, 0x7a, 0xfa, 0x54,
		0xe5, 0x17, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x89, 0x7f,
		0x57, 0x38, 0xc5, 0x4a, 0xe1, 0x71, 0x84, 0xab,
		0xa7, 0xd6, 0x38, 0x7d, 0x30, 0xd4, 0x9d, 0x3a,
		0x81, 0xb7, 0x72, 0x88, 0x91, 0x5f, 0xe7, 0xc8,
		0xc5, 0xbd, 0x50, 0xf7, 0x4c, 0xac, 0x5f, 0x2c,
		0x99, 0xd3, 0x8b, 0x97, 0x08, 0x71, 0xdb, 0x6c,
		0x1c, 0xf1, 0x43, 0x53, 0x76, 0x37, 0xce, 0x26,
		0xed, 0xf7, 0x67, 0x24, 0x47, 0x5e, 0xb9, 0x5c,
		0xe9, 0xcd, 0x96, 0x86, 0xaf, 0x66, 0xa9, 0xa6,
		0x42, 0x38, 0xa2, 0xc6, 0x36, 0x97, 0xf5, 0x11,
		0xaf, 0x66, 0xd0, 0xce, 0x22, 0x69, 0x89, 0x01,
		0xc3, 0xc2, 0x68, 0x65, 0x41, 0xf4, 0x0e, 0x76,
		0xc5, 0x5a, 0x61, 0xaf, 0x73, 0xbc, 0xdc, 0x45,
		0x4e, 0x6c, 0x13, 0x53, 0x60, 0x00, 0xbb, 0x81,
		0x75, 0xa4, 0xbd, 0xb5, 0xad, 0x10, 0x6a, 0x38,
		0x0c, 0x04, 0xa7, 0x65, 0x72, 0x5e, 0x93, 0x2d,
		0x24, 0x13, 0x94, 0xe0, 0x05, 0x11, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00,
		0x00, 0x00,
	};
	struct rsa_license_ops licnese_ops = {
		rsa_license_create_rivermax,
		rsa_license_info_rivermax,
		rsa_license_extract_rivermax,
	};
	struct rsa_license_data license_data;
	char *license = "test.lic";
	struct rsa_stream_init init;
	int ret = -1;

	/* test license create */
	license_data.version = FILE_FORMAT_VERSION;
	snprintf(license_data.vendor_name, VENDOR_NAME_MAX_LENGTH,
		"Ilan Smith");
	license_data.time_limit = get_time_limit(1);

	init.type = RSA_STREAM_TYPE_MEMORY;
	init.params.memory.buf = private_key_test;
	init.params.memory.len = ARRAY_SZ(private_key_test);

	ret = rsa_license_create(&init, license, &licnese_ops,
		&license_data);
	if (ret) {
		printf("rsa_license_create() failed\n");
		goto exit;
	}

	printf("\n");

	init.type = RSA_STREAM_TYPE_MEMORY;
	init.params.memory.buf = public_key_test;
	init.params.memory.len = ARRAY_SZ(public_key_test);

	/* test license info */
	ret = rsa_license_info(&init, license, &licnese_ops);
	if (ret) {
		printf("rsa_license_info() failed\n");
		goto exit;
	}

	printf("\n");

	/* test license extract */
	memset(&license_data, 0, sizeof(struct rsa_license_data));
	ret = rsa_license_extract(&init, license, &licnese_ops,
		&license_data);
	if (ret) {
		printf("rsa_license_extract() failed\n");
		goto exit;
	}
	printf("file format version extract: %llu\n", license_data.version);
	printf("vendor name extract: %s\n", license_data.vendor_name);
	printf("valid through extract: 0x" TIME_LIMIT_FMT "\n",
		license_data.time_limit);

	ret = 0;

exit:
	if (remove("test.lic"))
		ret = -1;

	return ret;
}

static int license_info(char key_path[FILE_NAME_MAX_LENGTH],
		char license[FILE_NAME_MAX_LENGTH])
{
	static unsigned char key_default[] = {
		0x49, 0x41, 0x53, 0x52, 0x53, 0x41, 0x51, 0xe0,
		0xcd, 0x77, 0xd2, 0xcf, 0xc3, 0x25, 0x30, 0x0d,
		0x9b, 0x7f, 0xf9, 0x01, 0x10, 0x1f, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
		0x00, 0x00, 0x7d, 0xac, 0xe4, 0x0a, 0x38, 0x3f,
		0x9d, 0x54, 0x2a, 0xc2, 0x1a, 0xa4, 0xc0, 0x51,
		0xef, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xad, 0xc7,
		0x3e, 0x44, 0x5f, 0xdb, 0x6f, 0x45, 0x0b, 0x65,
		0xcb, 0xd6, 0x01, 0x42, 0x70, 0x20, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
		0x00, 0x00, 0xbc, 0x25, 0x93, 0xd7, 0xea, 0x3a,
		0xa0, 0xa3, 0xb2, 0xae, 0xfa, 0xa5, 0x96, 0x7e,
		0x87, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0d, 0x39,
		0x5c, 0x65, 0x1e, 0x0b, 0xb6, 0xd5, 0x30, 0x8c,
		0xa6, 0x96, 0xb7, 0x6e, 0xc9, 0x0b, 0x3f, 0x80,
		0x5d, 0x76, 0x93, 0xe2, 0xb9, 0xff, 0xea, 0x68,
		0x6c, 0xf8, 0xe3, 0xf4, 0x75, 0x03, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00,
		0x00, 0x00, 0x9f, 0x52, 0x26, 0x6d, 0x1a, 0xee,
		0xe1, 0xb3, 0x0f, 0x9d, 0xeb, 0xdf, 0x64, 0xed,
		0xa3, 0x53, 0x49, 0x7e, 0xb1, 0x24, 0x0b, 0x21,
		0xc9, 0x7e, 0x30, 0x27, 0xf2, 0x72, 0xf3, 0x0f,
		0x20, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x86, 0x91,
		0x67, 0xb3, 0x48, 0xe1, 0x93, 0xd6, 0x59, 0x16,
		0xe0, 0x43, 0xa4, 0x6c, 0xb0, 0x79, 0x49, 0x2d,
		0xd0, 0x52, 0xce, 0x5a, 0x1c, 0x75, 0x77, 0xd4,
		0xb4, 0x2e, 0xe7, 0xaf, 0x8a, 0x01, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00,
		0x00, 0x00, 0x09, 0xc8, 0xaf, 0x2e, 0x44, 0x27,
		0x2e, 0x55, 0x93, 0x3c, 0x8b, 0xf1, 0x6d, 0x17,
		0x33, 0xbc, 0xca, 0x70, 0x86, 0xa1, 0x23, 0x01,
		0xae, 0xae, 0x85, 0xf9, 0xd9, 0xba, 0x09, 0x36,
		0x81, 0xec, 0xbe, 0x99, 0x68, 0x09, 0x45, 0xdd,
		0xfa, 0xfc, 0x85, 0xf7, 0x11, 0x14, 0xdb, 0x38,
		0xe2, 0x6a, 0x71, 0xc6, 0x2f, 0x7d, 0xb5, 0x41,
		0x53, 0x6a, 0x7c, 0xad, 0x83, 0x25, 0x40, 0x16,
		0x0d, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x0b, 0xe7,
		0xf7, 0x11, 0xc3, 0xcd, 0x95, 0x62, 0x2a, 0x78,
		0x3e, 0x32, 0xf5, 0xb5, 0xbd, 0x1a, 0x60, 0x85,
		0x1e, 0x77, 0x96, 0x1d, 0xfa, 0x4f, 0x5f, 0x76,
		0xd5, 0x32, 0x50, 0x28, 0x4e, 0xe5, 0x75, 0x94,
		0x88, 0x51, 0xa4, 0x45, 0xb6, 0xb6, 0xde, 0xfb,
		0xb7, 0xce, 0x32, 0xe5, 0xd9, 0xf7, 0x3b, 0x5c,
		0x69, 0x08, 0xb2, 0xe9, 0x68, 0xe9, 0x88, 0x34,
		0xc2, 0x0b, 0x0c, 0x14, 0x81, 0x09, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00,
		0x00, 0x00, 0xd5, 0x52, 0x47, 0xfd, 0x22, 0xb8,
		0x3a, 0x62, 0xee, 0xa6, 0x99, 0xda, 0xc5, 0x9d,
		0xa9, 0x7e, 0x1d, 0x5f, 0x99, 0xe4, 0xed, 0x62,
		0x83, 0xac, 0xd7, 0x32, 0x67, 0x0c, 0xf4, 0x60,
		0xbb, 0xb6, 0x30, 0x09, 0x99, 0x08, 0x30, 0x46,
		0xe3, 0xd3, 0x47, 0x16, 0x2e, 0x8a, 0x40, 0xdc,
		0xcc, 0x2b, 0xba, 0x74, 0x67, 0x1c, 0xd6, 0x8c,
		0x2e, 0x1a, 0x2c, 0xe2, 0xe8, 0xcc, 0xd3, 0x81,
		0x88, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x93, 0xf7,
		0x85, 0x79, 0x3b, 0xdd, 0x0e, 0x8e, 0x7a, 0x0a,
		0x78, 0x26, 0x6b, 0x63, 0x8f, 0xfc, 0xda, 0xae,
		0x07, 0xe7, 0xd2, 0xbb, 0x3c, 0x0c, 0x13, 0x78,
		0x89, 0x49, 0x85, 0xad, 0x88, 0x30, 0xa6, 0xb7,
		0x60, 0x35, 0x51, 0xfc, 0xbd, 0xa3, 0xbb, 0x06,
		0xc3, 0x17, 0xd4, 0x65, 0xa6, 0x99, 0xf8, 0xef,
		0x31, 0x39, 0x85, 0x53, 0x2a, 0x40, 0x08, 0xdc,
		0xe0, 0xf0, 0x6c, 0xa8, 0x12, 0x31, 0xcf, 0x99,
		0x5e, 0xd2, 0x96, 0x06, 0x68, 0x3a, 0x6a, 0x67,
		0x2e, 0xbd, 0x13, 0x01, 0x0f, 0x4f, 0x8d, 0x54,
		0xfb, 0x10, 0x60, 0x4e, 0xd0, 0x37, 0x16, 0x41,
		0x18, 0x68, 0x31, 0x42, 0xab, 0x74, 0x0a, 0x13,
		0x77, 0x26, 0x55, 0x9f, 0xb8, 0x9b, 0x62, 0x01,
		0x09, 0xc1, 0x7f, 0x19, 0x91, 0xac, 0x8d, 0xd1,
		0x2a, 0xe5, 0x89, 0xbb, 0xb0, 0x79, 0x5c, 0x06,
		0xbe, 0x67, 0xbe, 0x1a, 0x02, 0x01, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00,
		0x00, 0x00, 0xa5, 0xb9, 0x74, 0x42, 0xaa, 0x80,
		0x80, 0xce, 0x6d, 0x10, 0x51, 0x97, 0x09, 0x20,
		0x5b, 0x43, 0x11, 0x34, 0xca, 0xad, 0x82, 0x60,
		0xfd, 0x1e, 0x9b, 0x87, 0xed, 0xf6, 0x5a, 0x47,
		0xf7, 0x80, 0xe2, 0xb9, 0x5a, 0xa9, 0x49, 0xf5,
		0x5e, 0x94, 0xad, 0x2b, 0x1f, 0x80, 0xa9, 0xf9,
		0x0e, 0xab, 0xf7, 0x49, 0x8f, 0x25, 0x45, 0xbd,
		0xa1, 0x64, 0x37, 0xac, 0x06, 0x8d, 0xc5, 0x0f,
		0xee, 0x6c, 0x8c, 0x1f, 0x34, 0x75, 0x28, 0x12,
		0x74, 0xfb, 0x0d, 0x84, 0x3f, 0x0d, 0xff, 0x2e,
		0xb8, 0x08, 0x1b, 0x61, 0x0c, 0x4c, 0xb8, 0xe5,
		0x13, 0x48, 0x35, 0xf5, 0x31, 0x74, 0x33, 0x74,
		0xf8, 0x1f, 0x00, 0x39, 0x31, 0xe1, 0xe7, 0x78,
		0x70, 0x9d, 0x15, 0xe3, 0x2b, 0xe4, 0x70, 0xd4,
		0x25, 0xeb, 0xe9, 0x89, 0xb7, 0xba, 0x43, 0x36,
		0x33, 0x98, 0x48, 0x43, 0x0f, 0x7d, 0x6f, 0x0a,
		0x9b, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0xc3, 0x08,
		0x43, 0x18, 0x99, 0x3d, 0x53, 0x7b, 0x3b, 0x34,
		0xc4, 0x0d, 0x20, 0x8f, 0x7e, 0xe4, 0xeb, 0x52,
		0xbc, 0x02, 0x2c, 0x3b, 0x70, 0x8d, 0x41, 0x5c,
		0xab, 0x46, 0x95, 0xd1, 0xfb, 0xdc, 0xcf, 0x16,
		0x4a, 0x42, 0xde, 0x4f, 0x48, 0xa7, 0x6f, 0xdf,
		0x99, 0xe9, 0xa3, 0x3d, 0xb0, 0xc6, 0x82, 0x8a,
		0x75, 0xbf, 0xe8, 0x11, 0x90, 0xe9, 0x6d, 0x8d,
		0xf4, 0xb8, 0x23, 0x90, 0x38, 0x79, 0xab, 0xc3,
		0x56, 0xd3, 0x5b, 0x1a, 0xbb, 0xeb, 0x3a, 0x46,
		0x2f, 0x41, 0x2e, 0xe5, 0x44, 0x3c, 0xdf, 0x34,
		0xf4, 0xcc, 0x77, 0x81, 0xb0, 0x12, 0x78, 0xe2,
		0xdc, 0xd2, 0xdd, 0xa0, 0xb7, 0xa1, 0x5f, 0xbb,
		0x87, 0x19, 0x1b, 0x00, 0xae, 0x27, 0xfa, 0xa4,
		0x06, 0x02, 0xf3, 0xe2, 0xde, 0x3a, 0x01, 0xd1,
		0x74, 0x64, 0x6d, 0xb9, 0x08, 0xde, 0x53, 0xf3,
		0xe7, 0x8a, 0xf5, 0xe1, 0xfa, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00,
		0x00, 0x00,
	};
	struct rsa_license_ops licnese_ops = {
		rsa_license_create_rivermax,
		rsa_license_info_rivermax,
		rsa_license_extract_rivermax,
	};
	struct rsa_stream_init init;
	int ret;

	/* test license info */
	if (key_path[0]) {
		init.type = RSA_STREAM_TYPE_FILE;
		init.params.file.path = key_path;
		init.params.file.mode = "rb";
	} else {
		init.type = RSA_STREAM_TYPE_MEMORY;
		init.params.memory.buf = key_default;
		init.params.memory.len = ARRAY_SZ(key_default);
	}
	printf("Extracting license information for: %s\n", license);
	ret = rsa_license_info(&init, license, &licnese_ops);
	if (ret) {
		printf("rsa_license_info() failed\n");
		return -1;
	}

	return 0;
}

static int license_create(char private_key[FILE_NAME_MAX_LENGTH],
		char license[FILE_NAME_MAX_LENGTH],
		char vendor_name[VENDOR_NAME_MAX_LENGTH], time_t time_limit)
{
	struct rsa_license_ops licnese_ops = {
		rsa_license_create_rivermax,
		rsa_license_info_rivermax,
		rsa_license_extract_rivermax,
	};
	struct rsa_license_data license_data;
	struct rsa_stream_init init;
	int ret;

	/* test license create */
	license_data.version = FILE_FORMAT_VERSION;
	snprintf(license_data.vendor_name, VENDOR_NAME_MAX_LENGTH, "%s",
		vendor_name[0] ? vendor_name : VENDOR_NAME_DEFAULT);
	license_data.time_limit = time_limit;

	init.type = RSA_STREAM_TYPE_FILE;
	init.params.file.path = private_key;
	init.params.file.mode = "rb";
	ret = rsa_license_create(&init, license, &licnese_ops, &license_data);
	if (ret) {
		printf("rsa_license_create() failed\n");
		return -1;
	}

	printf("Created license %s:\n", license);
	printf("  License format version: %llu\n", license_data.version);
	printf("  Vendor name: %s\n", license_data.vendor_name);
	printf("  Valid through: %s\n",
		time_t_to_str(round_up_end_of_day_localtime(
			license_data.time_limit)));
	return 0;

}

int main(int argc, char **argv)
{
	int ret;
	unsigned long action;
	char key[FILE_NAME_MAX_LENGTH] = { 0 };
	char license[FILE_NAME_MAX_LENGTH];
	char vendor_name[VENDOR_NAME_MAX_LENGTH];
	time_t time_limit;

	rsa_license_init();

	if (parse_args(argc, argv, &action, key, license, vendor_name,
			&time_limit)) {
		return -1;
	}

	switch (action) {
	case OPT_FLAG(RSA_OPT_LIC_HELP):
		usage(basename(argv[0]));
		ret = 0;
		break;
	case OPT_FLAG(RSA_OPT_LIC_CREATE):
		ret = license_create(key, license, vendor_name, time_limit);
		break;
	case OPT_FLAG(RSA_OPT_LIC_INFO):
		ret = license_info(key, license);
		break;
	case OPT_FLAG(RSA_OPT_LIC_TEST):
		ret = license_test();
		break;
	default:
		ret = -1;
		break;
	}

	return ret;
}

