
#include "sisfile.h"
#include "sisinstaller.h"
#include "psion.h"
#include "fakepsion.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#if HAVE_LIBNEWT
# include <newt.h>
#endif

#define _GNU_SOURCE
#include <getopt.h>

bool usenewt = false;

static void error(int line)
{
#if HAVE_LIBNEWT
	if (usenewt)
		{
		newtPopHelpLine();
		newtPushHelpLine(_("Got an error, Press any key to exit."));
		newtWaitForKey();
		newtFinished();
		}
#endif
	fprintf(stderr, _("Error %d on line %d: %s\n"), errno, line,
		strerror(errno));
	exit(1);
}

static struct option opts[] = {
	{ "help",     no_argument,       0, 'h' },
	{ "version",  no_argument,       0, 'V' },
	{ "verbose", required_argument, 0, 'v' },
	{ "dry-run",  no_argument,       0, 'n' },
#if HAVE_LIBNEWT
	{ "newt",     no_argument,       0, 'w' },
#endif
	{ NULL,       0,                 0, 0 },
};

void printHelp()
{
	printf(
	_("Usage: sisinstall [OPTIONS]... SISFILE\n"
	"\n"
	"Supported options:\n"
	"\n"
	" -h, --help              Display this text.\n"
	" -V, --version           Print version and exit.\n"
	" -v, --verbose=LEVEL     Set the verbosity level, by default 0.\n"
	" -n, --dry-run           Just parse file file.\n"
#if HAVE_LIBNEWT
	" -w, --newt              Use the Newt interface.\n"
#endif
	));
}

void main(int argc, char* argv[])
{
	char* filename = 0;
	char option;
	bool dryrun = false;

#ifdef LC_ALL
	setlocale(LC_ALL, "");
#endif
	textdomain(PACKAGE);

	while (1)
		{
		option = getopt_long(argc, argv,
#if HAVE_LIBNEWT
							 "hnv:Vw"
#else
							 "hnv:V"
#endif
							 , opts, NULL);
		if (option == -1)
			break;
		switch (option)
			{
			case 'h':
			case '?':
				printHelp();
				exit(0);
			case 'v':
				logLevel = atoi(optarg);
				break;
			case 'n':
				dryrun = true;
				break;
#if HAVE_LIBNEWT
			case 'w':
				usenewt = true;
				break;
#endif
			case 'V':
				printf(_("sisinstall version 0.1\n"));
				exit(0);
			}
		}
#if HAVE_LIBNEWT
	if (usenewt)
		{
		newtInit();
		newtCls();
		}
#endif
	if (optind < argc)
	{
		filename = argv[optind];
#if HAVE_LIBNEWT
		if (usenewt)
			{
			char helpline[256];
			sprintf(helpline,
					_("Installing sis file %s%s.\n"),
					filename,
				   dryrun ? _(", not really") : "");
			newtPushHelpLine(helpline);
//			newtWaitForKey();
			}
		else
#endif
			printf(_("Installing sis file %s%s.\n"), filename,
				   dryrun ? _(", not really") : "");
	}
	else
	{
#if HAVE_LIBNEWT
	    if (usenewt)
		newtFinished();
#endif
	    fprintf(stderr, _("Missing SIS filename\n"));
	    exit(1);
	}
	struct stat st;
	if (-1 == stat(filename, &st))
		error(__LINE__);
	off_t len = st.st_size;
	if (logLevel >= 2)
		printf(_("File is %d bytes long\n"), len);
	uint8_t* buf = new uint8_t[len];
	int fd = open(filename, O_RDONLY);
	if (-1 == fd)
		error(__LINE__);
	if (-1 == read(fd, buf, len))
		error(__LINE__);
	close(fd);
	Psion* psion;
	if (dryrun)
		psion = new FakePsion();
	else
		psion = new Psion();
	if (!psion->connect())
		{
		printf(_("Couldn't connect with the Psion\n"));
		}
	else
		{
		createCRCTable();
		SISFile sisFile;
		SisRC rc = sisFile.fillFrom(buf, len);
		if (rc == SIS_OK)
			{
//			if (!dryrun)
				{
				SISInstaller installer;
				installer.setPsion(psion);
#if HAVE_LIBNEWT
				installer.useNewt(usenewt);
#endif
				installer.run(&sisFile, buf, len);
				}
			}
		else
			{
			printf(_("Could not parse the sis file.\n"));
			}
		psion->disconnect();
		}

#if HAVE_LIBNEWT
	if (usenewt)
		{
		newtPopHelpLine();
		newtPushHelpLine(_("Installation complete. Press any key to exit."));
		newtWaitForKey();
		newtFinished();
		}
#endif
	exit(0);
}

