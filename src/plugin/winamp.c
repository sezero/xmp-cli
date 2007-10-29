/*
 * XMP plugin for WinAmp
 *
 * $Id: winamp.c,v 1.1 2007-10-25 23:12:00 cmatsuoka Exp $
 */

#include <windows.h>
#include <mmreg.h>
#include <msacm.h>
#include <math.h>

#include "in2.h"

extern In_Module mod;
char lastfn[MAX_PATH];
short sample_buffer[576 * 2];

// avoid CRT. Evil. Big. Bloated.
BOOL WINAPI
_DllMainCRTStartup(HANDLE hInst, ULONG ul_reason_for_call, LPVOID lpReserved)
{
	return TRUE;
}

int paused;
DWORD WINAPI __stdcall PlayThread(void *b);
int gen_freq = 2600;
int killDecodeThread = 0;
HANDLE thread_handle = INVALID_HANDLE_VALUE;


#define FREQ_SAMPLE_44 0
#define FREQ_SAMPLE_22 1
#define FREQ_SAMPLE_11 2

typedef struct {
	int mixing_freq;
	int force8bit;
	int force_mono;
	int interpolation;
	int filter;
	int convert8bit;
	int fixloops;
	int loop;
	int modrange;
	int pan_amplitude;
	int time;
	struct xmp_module_info mod_info;
} XMPConfig;


XMPConfig xmp_cfg;



void config(HWND hwndParent)
{
	MessageBox(hwndParent, "Later.", "Configuration", MB_OK);
}


void about(HWND hwndParent)
{
	MessageBox(hwndParent, "Later.", "About XMP", MB_OK);
}


void init()
{
	ConfigFile *cfg;
	char *filename;

	ctx = xmp_create_context();

	xmp_cfg.mixing_freq = 0;
	xmp_cfg.convert8bit = 0;
	xmp_cfg.fixloops = 0;
	xmp_cfg.modrange = 0;
	xmp_cfg.force8bit = 0;
	xmp_cfg.force_mono = 0;
	xmp_cfg.interpolation = TRUE;
	xmp_cfg.filter = TRUE;
	xmp_cfg.pan_amplitude = 80;

#if 0
#define CFGREADINT(x) xmms_cfg_read_int (cfg, "XMP", #x, &xmp_cfg.x)

	filename = g_strconcat(g_get_home_dir(), CONFIG_FILE, NULL);

	if ((cfg = xmms_cfg_open_file(filename))) {
		CFGREADINT(mixing_freq);
		CFGREADINT(force8bit);
		CFGREADINT(convert8bit);
		CFGREADINT(modrange);
		CFGREADINT(fixloops);
		CFGREADINT(force_mono);
		CFGREADINT(interpolation);
		CFGREADINT(filter);
		CFGREADINT(pan_amplitude);

		xmms_cfg_free(cfg);
	}
#endif

	xmp_init_callback(ctx, driver_callback);
	//xmp_register_event_callback(x11_event_callback);
}


void quit()
{
}


int is_our_file(char *fn)
{
	if (xmp_test_module(ctx, filename, NULL) == 0)
		return 1;

	return 0;
}


int play(char *fn)
{
	int maxlatency;
	int tmp;
	int channelcnt = 1;
	int format = FMT_U8;
	FILE *f;
	struct xmp_options *opt;
	int lret;
	int nch;
	
	strcpy(lastfn, fn);

	opt = xmp_get_options(ctx);

	stop();		/* sanity check */

	if ((f = fopen(filename,"rb")) == 0) {
		playing = 0;
		return;
	}
	fclose(f);

	xmp_plugin_audio_error = FALSE;
	playing = 1;

	opt->resol = 8;
	opt->verbosity = 3;
	opt->drv_id = "callback";

	switch (xmp_cfg.mixing_freq) {
	case 1:
		opt->freq = 22050;	/* 1:2 mixing freq */
		break;
	case 2:
		opt->freq = 11025;	/* 1:4 mixing freq */
		break;
	default:
		opt->freq = 44100;	/* standard mixing freq */
		break;
	}

	if ((xmp_cfg.force8bit == 0)) {
		format = FMT_S16_NE;
		opt->resol = 16;
	}

	if ((xmp_cfg.force_mono == 0)) {
		channelcnt = 2;
	} else {
		opt->outfmt |= XMP_FMT_MONO;
	}

	if ((xmp_cfg.interpolation == 1))
		opt->flags |= XMP_CTL_ITPT;

	if ((xmp_cfg.filter == 1))
		opt->flags |= XMP_CTL_FILTER;

	opt->mix = xmp_cfg.pan_amplitude;

	fmt = opt->resol == 16 ? FMT_S16_NE : FMT_U8;
	nch = opt->outfmt & XMP_FMT_MONO ? 1 : 2;
	
	if (audio_open)
	    mod.outMod->Close();
	audio_open = TRUE;

	paused = 0;
	memset(sample_buffer, 0, sizeof(sample_buffer));
	maxlatency = mod.outMod->Open(44100, 1, 16, -1, -1);
	if (maxlatency < 0)
		return 1;
	mod.SetInfo(0, 44, 1, 1);
	mod.SAVSAInit(maxlatency, 44100);
	mod.VSASetInfo(44100, 1);
	mod.outMod->SetVolume(-666);

	
	xmp_open_audio(ctx);

	pthread_mutex_lock(&load_mutex);
	lret =  xmp_load_module(ctx, filename);
	pthread_mutex_unlock(&load_mutex);

	xmp_cfg.time = lret;
	xmp_get_module_info(ctx, &ii->mi);
	strcpy(ii->filename, "");

	new_module = 1;

	//xmp_ip.set_info(ii->mi.name, lret, 0, opt->freq, channelcnt);


	killDecodeThread = 0;
	thread_handle = (HANDLE)CreateThread(NULL, 0,
			(LPTHREAD_START_ROUTINE) PlayThread,
			(void *)&killDecodeThread, 0, &tmp);
	return 0;
}


void pause()
{
	paused = 1;
	mod.outMod->Pause(1);
}


void unpause()
{
	paused = 0;
	mod.outMod->Pause(0);
}


int is_paused()
{
	return paused;
}


void stop()
{
	if (!playing)
		return;

	xmp_stop_module();

	if (thread_handle != INVALID_HANDLE_VALUE) {
		killDecodeThread = 1;
		if (WaitForSingleObject(thread_handle, INFINITE) ==
		    WAIT_TIMEOUT) {
			MessageBox(mod.hMainWindow,
				   "error asking thread to die!\n",
				   "error killing decode thread", 0);
			TerminateThread(thread_handle, 0);
		}
		CloseHandle(thread_handle);
		thread_handle = INVALID_HANDLE_VALUE;
	}
	mod.outMod->Close();
	mod.SAVSADeInit();
}


int getlength()
{
	return -1000;
}


int getoutputtime()
{
	return mod.outMod->GetOutputTime();
}


void setoutputtime(int time_in_ms)
{
}


void setvolume(int volume)
{
	mod.outMod->SetVolume(volume);
}


void setpan(int pan)
{
	mod.outMod->SetPan(pan);
}


int infoDlg(char *fn, HWND hwnd)
{
	return 0;
}


void get_file_info(char *filename, char *title, int *length_in_ms)
{
	char *x;

	_D("song_info: %s", filename);
	if ((x = strrchr (filename, '\\')))
		filename = ++x;
	wsprintf(title, "%s", title);

}


void eq_set(int on, char data[10], int preamp)
{
}


In_Module mod = {
	IN_VER,
	"XMP Plugin " VERSION,
	0,			// hMainWindow
	0,			// hDllInstance
	"\0", 0,		// is_seekable
	1,			// uses output
	config,
	about,
	init,
	quit,
	get_file_info,
	infoDlg,
	is_our_file,
	play,
	pause,
	unpause,
	is_paused,
	stop,
	getlength,
	getoutputtime,
	setoutputtime,
	setvolume,
	setpan,
	0, 0, 0, 0, 0, 0, 0, 0, 0,	// vis stuff
	0, 0,			// dsp
	eq_set, NULL,		// setinfo
	0			// out_mod
};

__declspec(dllexport)
In_Module *winampGetInModule2()
{
	return &mod;
}


int _fltused = 0;
DWORD WINAPI __stdcall PlayThread(void *b)
{
	while (!*((int *)b)) {
		if (mod.outMod->CanWrite() >= ((sizeof(sample_buffer) / 2) <<
						(mod.dsp_isactive()? 1 : 0))) {
			{
				int t = mod.outMod->GetWrittenTime();
				mod.SAAddPCMData((char *)sample_buffer, 1, 16,
						 t);
				mod.VSAAddPCMData((char *)sample_buffer, 1, 16,
						  t);
			}

			l = mod.dsp_dosamples(sample_buffer, l / 2, 16,
					      1, 44100) * 2;

			mod.outMod->Write(sample_buffer, l);
		} else {
			Sleep(50);
		}
	}

	return 0;
}