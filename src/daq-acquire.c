/*
 * daq-acquire: small utility to acquire samples with Comedi-supported DAQ cards
 * Copyright(c) 2013-2016 by Iñaki Ucar <i.ucar86@gmail.com>
 * This program is published under a MIT license
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sched.h>
#include <errno.h>
#include <comedilib.h>
#include <sys/mman.h>

#define N_CHANS 256
#define BUFSZ 100000

struct parsed_options {
	char 		*filename;
	int 		subdevice;
	int 		channel[N_CHANS];
	int 		n_chan;
	int 		aref;
	int 		range;
	double 		freq;
	int 		n_scan;
	int 		verbose;
	int 		integrate;
	int 		fulltime;
};

struct parsed_options options = {
	.filename = "/dev/comedi0",
	.subdevice = 0,
	.channel[0] = 0,
	.n_chan = 1,
	.aref = AREF_GROUND,
	.range = 0,
	.freq = 10000.0,
	.n_scan = 0,
	.integrate = 1,
	.verbose = 0,
	.fulltime = 0,
};

void parse_options(int argc, char *argv[]);
int prepare_cmd_lib(comedi_t *dev, comedi_cmd *cmd, unsigned int chanlist[], unsigned scan_period_nanosec);
unsigned int double_check_cmd(comedi_t *dev, comedi_cmd *cmd);
long double clock_init();
void print_datum(long double init, unsigned int period, lsampl_t raw, const comedi_polynomial_t *converter, int *n);
int get_converter(comedi_t *device, comedi_polynomial_t *converter, int flags);
void set_sched() {
	struct sched_param param;
	cpu_set_t mask;

	sched_getparam(0, &param);
	param.sched_priority = sched_get_priority_max(SCHED_FIFO);
	sched_setscheduler(0, SCHED_FIFO, &param);
	sched_getaffinity(0, sizeof(cpu_set_t), &mask);
	CPU_ZERO(&mask);
	CPU_SET(sched_getcpu(), &mask);
	sched_setaffinity(0, sizeof(cpu_set_t), &mask);
}

int main(int argc, char *argv[]) {
	comedi_t 		*dev;
	comedi_cmd 		cmd;
	unsigned int 		chanlist[N_CHANS];
	lsampl_t 		raw;
	comedi_polynomial_t 	converter;
	unsigned int		real_period;
	int 			ret, i, subdev_flags;
	int 			bytes_per_sample, size;
	int			n, back, front;
	void  			*map;
	long double		init;

	parse_options(argc, argv);
	//set_sched();

	/* open the device */
	dev = comedi_open(options.filename);
	if (!dev) {
		comedi_perror(options.filename);
		exit(1);
	}

	// flags & bytes per sample
	subdev_flags = comedi_get_subdevice_flags(dev, options.subdevice);
	if (subdev_flags < 0) {
		comedi_perror("comedi_get_subdevice_flags");
		exit(1);
	}
	if (subdev_flags & SDF_LSAMPL)
		bytes_per_sample = sizeof(lsampl_t);
	else bytes_per_sample = sizeof(sampl_t);

	// get converter from calibration file
	ret = get_converter(dev, &converter, subdev_flags);
	if (ret < 0) exit(1);

	// prepare mmap
	size = comedi_get_buffer_size(dev, options.subdevice);
	map = mmap(NULL, size, PROT_READ, MAP_SHARED, comedi_fileno(dev), 0);
	if (map == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	/* Set up channel list */
	for (i = 0; i < options.n_chan; i++) {
		chanlist[i] = CR_PACK(options.channel[i], options.range, options.aref);
	}

	/* prepare_cmd_lib() uses a Comedilib routine to find a
	 * good command for the device.  prepare_cmd() explicitly
	 * creates a command, which may not work for your device. */
	prepare_cmd_lib(dev, &cmd, chanlist, 1e9 / options.freq);

	/* test the command */
	real_period = double_check_cmd(dev, &cmd);

	/* start the command */
	init = clock_init();
	ret = comedi_command(dev, &cmd);
	if (ret < 0) {
		comedi_perror("comedi_command");
		exit(1);
	}

	/* read */
	front = back = n = 0;
	while(1) {
		front += comedi_get_buffer_contents(dev, options.subdevice);
		if(options.verbose) fprintf(stderr, "front = %d, back = %d\n", front, back);
		if(front < back) break;
		if (front == back) {
			if (options.n_scan > 0 && n >= options.n_scan) break;
			//comedi_poll(dev, options.subdevice);
			usleep(10000);
			continue;
		}

		for (i = back; i < front; i += bytes_per_sample) {
			if (subdev_flags & SDF_LSAMPL)
				raw = *(lsampl_t *)((char*)map + (i % size));
			else raw = *(sampl_t *)((char*)map + (i % size));
			print_datum(init, real_period, raw, &converter, &n);
		}

		ret = comedi_mark_buffer_read(dev, options.subdevice, front - back);
		if (ret < 0) {
			comedi_perror("comedi_mark_buffer_read");
			break;
		}
		back = front;
	}
	comedi_close(dev);

	return 0;
}

void help() {
    fprintf(stderr,
		"Small utility to acquire samples with Comedi-supported DAQ cards.\n"
		"\n"
		"Usage: ./daq-acquire [options]\n"
		"\n"
		"Options:\n"
		"  -h           show help\n"
		"  -v           verbose\n"
		"  -T           full timestamp\n"
		"  -d <dev>     device file                default: /dev/comedi0\n"
		"  -s <id>      subdevice id               default: 0\n"
		"  -c <list>    channel list (by commas)   default: 0\n"
		"  -a <id>      aref id                    default: 0 -> AREF_GROUND\n"
		"  -r <id>      range id                   default: 0 -> [-10, 10]\n"
		"  -f <freq>    frequency                  default: 10000\n"
		"  -n <num>     number of samples          default: infinity\n"
		"  -I <num>     integration samples        default: 1\n"
		"\n"
    );
}

void info() {
    comedi_range *range_info;
    comedi_t *dev = comedi_open(options.filename);
    if (!dev) {
    	comedi_perror(options.filename);
    	exit(1);
    }

    fprintf(stderr, "Selected device: %s | Driver: %s\n\n", comedi_get_board_name(dev), comedi_get_driver_name(dev));

    int i, n, type, flags;
    type = comedi_get_subdevice_type(dev, options.subdevice);
    flags = comedi_get_subdevice_flags(dev, options.subdevice);

    fprintf(stderr, "Selected subdevice: %i\n", options.subdevice);

    if ((n = comedi_get_n_ranges(dev, options.subdevice, 0)) > -1) {
		fprintf(stderr, "  - Range(id): ");
		for (i=0; i<n; i++) {
			range_info = comedi_get_range(dev, options.subdevice, 0, i);
			fprintf(stderr, "[%g, %g](%i) ", range_info->min, range_info->max, i);
		}
		fprintf(stderr, "\n");
    }

	if (type == COMEDI_SUBD_AI || type == COMEDI_SUBD_AO) {
		fprintf(stderr, "  - ARef(id) : ");
		if (flags & SDF_GROUND)
			fprintf(stderr, "AREF_GROUND(%i) ", AREF_GROUND);
		if (flags & SDF_COMMON)
			fprintf(stderr, "AREF _COMMON(%i) ", AREF_COMMON);
		if (flags & SDF_DIFF)
			fprintf(stderr, "AREF_DIFF(%i) ", AREF_DIFF);
		if (flags & SDF_OTHER)
			fprintf(stderr, "AREF_OTHER(%i) ", AREF_OTHER);
		fprintf(stderr, "\n");
	}
	fprintf(stderr, "\n");

	if (type == COMEDI_SUBD_AI || type == COMEDI_SUBD_DI || type == COMEDI_SUBD_DIO) {
		fprintf(stderr, "Selected channels: ");
		for (i=0; i<options.n_chan; i++)
			fprintf(stderr, "%i ", options.channel[i]);
		fprintf(stderr, "\n\n");
	}

	const char * const subdevice_types[] = {
		"| (unused)    ",
		"| AI          ",
		"| AO          ",
		"| DI          ",
		"| DO          ",
		"| DIO         ",
		"| Counter     ",
		"| Timer       ",
		"| Memory      ",
		"| Calibration ",
		"| Processor   ",
		"| Serial IO   ",
		"| PulseWidthM "
	};
	const char * const subdevice_unknown[] = {
		"| (error)     ",
		"| (unknown)   "
	};

    fprintf(stderr, "Subdev | Type        | Buffer   | Channels | Ranges   \n");
    fprintf(stderr, "------------------------------------------------------\n");
    n = comedi_get_n_subdevices(dev);
    for (i=0; i<n; i++) {
    	fprintf(stderr, "%6i ", i);

    	type = comedi_get_subdevice_type(dev, i);
    	if (type < 0)
    		fputs(subdevice_unknown[0], stderr);
    	else if (0 <= type && type < (int)(sizeof(subdevice_types) / sizeof(subdevice_types[0])))
    		fputs(subdevice_types[type], stderr);
		else
			fputs(subdevice_unknown[1], stderr);

    	fprintf(stderr, "| %8i ", comedi_get_buffer_size(dev, i));
    	fprintf(stderr, "| %8i ", comedi_get_n_channels(dev, i));
    	fprintf(stderr, "| %8i ", comedi_get_n_ranges(dev, i, 0));
    	fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");

    comedi_close(dev);
}

void parse_options(int argc, char *argv[]) {
	int c, n=0, h=0;
	char *pt;

	while (-1 != (c = getopt(argc, argv, "hvTd:s:c:a:r:f:n:I:"))) {
		switch (c) {
		case 'h': //help
			h = 1;
			break;
		case 'v': //verbose
			options.verbose = 1;
			break;
		case 'T': //verbose
			options.fulltime = 1;
			break;
		case 'd': //device
			options.filename = optarg;
			break;
		case 's': //subdevice
			options.subdevice = strtoul(optarg, NULL, 0);
			break;
		case 'c': //channel list
			pt = strtok (optarg, ",");
			while (pt != NULL) {
				options.channel[n] = atoi(pt);
				pt = strtok (NULL, ",");
				n++;
			}
			options.n_chan = n;
			break;
		case 'a': //aref
			options.aref = strtoul(optarg, NULL, 0);
			break;
		case 'r': //range
			options.range = strtoul(optarg, NULL, 0);
			break;
		case 'f': //frequency
			options.freq = strtod(optarg, NULL);
			break;
		case 'n': //number of samples
			options.n_scan = strtoul(optarg, NULL, 0);
			if (options.n_scan < 0) options.n_scan = 0;
			break;
		case 'I': //integration period
			options.integrate = strtoul(optarg, NULL, 0);
			if (options.integrate <= 0) options.integrate = 1;
			break;
		}
	}
	if (h) {
		help();
		if (options.verbose) info();
		exit(1);
	}
}

/*
 * This prepares a command in a pretty generic way.  We ask the
 * library to create a stock command that supports periodic
 * sampling of data, then modify the parts we want. */
int prepare_cmd_lib(comedi_t *dev, comedi_cmd *cmd, unsigned int chanlist[], unsigned scan_period_nanosec) {
	int ret;

	memset(cmd, 0, sizeof(*cmd));

	/* This comedilib function will get us a generic timed
	 * command for a particular board.  If it returns -1,
	 * that's bad. */
	ret = comedi_get_cmd_generic_timed(dev, options.subdevice, cmd, options.n_chan, scan_period_nanosec);
	if (ret < 0) {
		fprintf(stderr, "prepare_cmd_lib: comedi_get_cmd_generic_timed failed\n");
		return ret;
	}

	/* Modify parts of the command */
	cmd->chanlist = chanlist;
	cmd->chanlist_len = options.n_chan;
	if (options.n_scan > 0)
		cmd->stop_src = TRIG_COUNT;
	else
		cmd->stop_src = TRIG_NONE; // captura continua
	cmd->stop_arg = options.n_scan;

	return 0;
}

unsigned int double_check_cmd(comedi_t *dev, comedi_cmd *cmd) {
	/* comedi_command_test() tests a command to see if the
	 * trigger sources and arguments are valid for the subdevice.
	 * If a trigger source is invalid, it will be logically ANDed
	 * with valid values (trigger sources are actually bitmasks),
	 * which may or may not result in a valid trigger source.
	 * If an argument is invalid, it will be adjusted to the
	 * nearest valid value.  In this way, for many commands, you
	 * can test it multiple times until it passes.  Typically,
	 * if you can't get a valid command in two tests, the original
	 * command wasn't specified very well. */
	int ret = comedi_command_test(dev, cmd);
	if (ret < 0) {
		comedi_perror("comedi_command_test");
		if (errno == EIO) {
			fprintf(stderr,"double_check_cmd: ummm... this subdevice doesn't support commands\n");
		}
		exit(1);
	}
	ret = comedi_command_test(dev, cmd);
	if (ret < 0) {
		comedi_perror("comedi_command_test");
		exit(1);
	}
	fprintf(stderr,"double_check_cmd: command succesfully tested\n");
	if (ret != 0) {
		fprintf(stderr, "double_check_cmd: error preparing command\n");
		exit(1);
	}
	if (options.verbose) fprintf(stderr, "double_check_cmd: cmd->scan_begin_arg = %u ns\n", cmd->scan_begin_arg);
	return cmd->scan_begin_arg;
}

long double clock_init() {
	static long double init;
	struct timespec ts;

	if (!init) {
		if (clock_gettime(CLOCK_REALTIME, &ts)) {
			    perror("clock_gettime");
			    exit(1);
		}
		init = ts.tv_sec+ts.tv_nsec/1000000000.0L;
	}

	return init;
}

void print_datum(long double init, unsigned int period, lsampl_t raw, const comedi_polynomial_t *converter, int *n) {
	static long double t; //seconds
	static int col = 0;
	static int isamples;
	static double buf[N_CHANS];

	if (!isamples) isamples = options.integrate;

	buf[col] += comedi_to_physical(raw, converter);

	if (++col == options.n_chan) {
		col = 0;
		if (!--isamples) {
			//time
			if (!options.fulltime) printf("%.7Lf ", t);
			else printf("%.7Lf ", init+t);

			//measures
			for (int i=0; i<options.n_chan; i++) {
				printf("%8.6f ", buf[i]/((double)options.integrate));
				buf[i] = 0;
			}

			//end
			printf("\n");
			//isamples = options.integrate;
		}
		(*n)++;
		t = t + period/(long double)1e9;
	}
}

/* figure out if we are talking to a hardware-calibrated or software-calibrated board,
	then obtain a comedi_polynomial_t which can be used with comedi_to_physical */
int get_converter(comedi_t *device, comedi_polynomial_t *converter, int flags)
{
	int retval;

	if(flags & SDF_SOFT_CALIBRATED) /* board uses software calibration */
	{
		char *calibration_file_path = comedi_get_default_calibration_path(device);

		/* parse a calibration file which was produced by the
			comedi_soft_calibrate program */
		comedi_calibration_t* parsed_calibration =
			comedi_parse_calibration_file(calibration_file_path);
		free(calibration_file_path);
		if(parsed_calibration == NULL)
		{
			comedi_perror("comedi_parse_calibration_file");
			return -1;
		}

		/* get the comedi_polynomial_t for the subdevice/channel/range
			we are interested in */
		retval = comedi_get_softcal_converter(options.subdevice, options.channel[0], options.range,
			COMEDI_TO_PHYSICAL, parsed_calibration, converter);
		comedi_cleanup_calibration(parsed_calibration);
		if(retval < 0)
		{
			comedi_perror("comedi_get_softcal_converter");
			return -1;
		}
	}else /* board uses hardware calibration */
	{
		retval = comedi_get_hardcal_converter(device, options.subdevice, options.channel[0], options.range,
			COMEDI_TO_PHYSICAL, converter);
		if(retval < 0)
		{
			comedi_perror("comedi_get_hardcal_converter");
			return -1;
		}
	}

	return 0;
}
