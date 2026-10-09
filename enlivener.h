/* ==========================================================================
 * Enlivener - A Time-Domain Live Guitar Tracker for Linux
 * File: enlivener.h
 * Description: Listens to incoming guitar audio, tracks fundamentals in the 
 *              time domain, and generates live, consonant backing music.
 * Author: Cynthia Christie - https://www.youtube.com/@enlivener-d7f
 * License: Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)
 * ========================================================================== */

// A note about this file:
//
// Due to the suddenly high priority of getting Enlivener out the door, spurred
// by the realization that I have done something fairly unique here and that
// I may miss the opportunity to licence it under terms that are acceptable
// to me, which are the:
//
// Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International Public
// License
//
// for reasons that are beyond the scope of this note, the quality of the 
// code commentary in this file suffers as compared with the relative
// exacting way that this was done in enlivener.c.  I am amenable to questions if 
// approached in a normal way with no annoying reminders that I am a
// biological male.  Also, nothing in this file is rocket science so y'all
// will be fine, I'm sure.
//
// Cynthia Christie - Oct 6 2026

#define	SYNTH_EXECUTABLE	"./enlivener-synth"


#define SUSTAIN_FUNDAMENTAL	(rate*8)
#define SUSTAIN_OCTAVE		(rate*6)
#define SUSTAIN_THIRD		(rate*2)
#define SUSTAIN_FOURTH		(rate*3)
#define SUSTAIN_FIFTH		(rate*4)

#define WAVE_FUNDAMENTAL	0
#define WAVE_OCTAVE		1
#define WAVE_THIRD		2
#define WAVE_FOURTH		3
#define WAVE_FIFTH		4

#define WAV_HEADER_SIZE 44


#define SINE_CHECK_LEVEL 256

#define OLDEST_TONES	3000000000 


#define WINNER_CAP	((max_tone*MAX_PHASE)/50)

#define PHASE_PERCENT	100

#define HUMANIZE_VOL	2048
#define HUMANIZE_TIME	256	// Behaviour is "undefined" if not even


#define BPM_MIN		60
#define BPM_MAX		240



#define AUDIO_READ_TIME	(60000000000/((bpm)*2))
#define AUDIO_READ	((AUDIO_READ_TIME*rate)/1000000000)

#define MIN_ANALYSE	2048
#define MAX_ANALYSE	22050

#define MAIN_PROC_ALLOW	100000000



#define	QUIET_ITER	16

#define MISC_TEXT	256

#define PORT_FMT		"/tmp/enlivener-%d.port"
#define DRUM_FMT		"/tmp/enlivener-%d.drum" // %d - geteuid()

#define	SHM_SYNTH_FMT			"/enlivener-synth-%d"
#define SHM_FRONT_FMT			"/enlivener-front-%d"
#define	SHM_PLAYER_FMT			"/enlivener-player-%d"

#define NOTE_HISTORY_LIMIT	18
#define NOTE_WEIGHT_MULTIPLIER	1



#define FRONT_BASS_VOLUME	50
#define	FRONT_DRUM_VOLUME	50

#define MAX_NO_SOUND	24


#define CMD_TONE	0x01
#define CMD_EXIT	0x02
#define CMD_READ_DRUMS	0x04
#define CMD_QUIET	0x08
#define	CMD_RESUME	0x10

#define BEND_CHANCE	2


#define NANONANO	((tp.tv_sec*1000000000)+tp.tv_nsec)

#define MAX_SYNTH_CHECKS	6

#define MUTE_EARLY		(rate/16)

#define TRANSVERSE_LEVEL	778
#define TRANSVERSE_VARIANCE	384
#define TRANS_VARIANCE_MIN	128

#define MIN_ADVANCE_SAMPLES	256

#define SINE_QUARTER	AUDIO_READ
#define SINE_EIGHTH	(AUDIO_READ/2)
#define SINE_SIXTEENTH	(AUDIO_READ/4)	// per above will fade this entire note


#define BASS_FADE	1024
#define BASS_FADE_FLOOR	512

#define	MUTE_PREVIOUS		1536
#define MUTE_PREVIOUS_MIN	512
#define BASS_ATTACK_LEN	(SINE_SIXTEENTH/2)

#define SHARP_ATTACK	(BASS_ATTACK_LEN)

#define BASS_REF_VAL	(16384+8192)

#define BASS_PEAK	(16384+4096)

#define BASS_ATTACK	(3072)

#define BASS_ATTACK_MIN	(BASS_ATTACK/2)

#define HARMONIC_FLOOR	8192

#define BASS_CLIPVAL	1.75
#define T_CLIPVAL	2.5

#define BASS_LEAD_IN	(SINE_SIXTEENTH/2)

#define MAX_TRANS_DELAY	96


#define MAX_PHASE	16

#define	BEND_MAX 7

#define IS_QUARTER	0x00
#define	IS_EIGHTH	0x01
#define IS_SIXTEENTH	0x02

typedef struct	{
	int	tone;
	long	played_at;
} NOTE_HISTORY;



typedef struct {
	bool	play_ok;
	int	rate;
	short	audio[];
} PLAYER_SHM;

typedef struct {
	short	*fundamental;
	short	*octave;
	short	*third;
	short	*fourth;
	short	*fifth;
} TRANSVERSE;

typedef struct {
	short	*whole;		// I was tempted to call this "ounce"
	short	*half;
	short	*quarter;
	short	*eighth;	
} ICING;

#define ICING_LEVEL	8192
#define	ICING_OFFSET	256

typedef struct {
	int tone;
	int weight;
} NOTE_ENTRY;

typedef struct {
	bool	triad;
	int	tones[3];
	int	accents_count;
	int	accents[12];
	int	key_is;
	long	oldest_note;
} KEY_INFO;

typedef struct {
	int	transverses;
	int	roots;
} NOTE_CHECK;


typedef struct {
	short		*sine_buf;
	int		period;
	int		note;
	int 		tone;
	int		offset;
	unsigned long	benefit;
} PHASE;

typedef struct {
	double		frequency;
	int		period;
	PHASE		phase[MAX_PHASE];
	short		*sine_buf;
} FREQ_FRONT;

typedef struct {
	TRANSVERSE	quarter[BEND_MAX];
	TRANSVERSE	eighth[BEND_MAX];
	TRANSVERSE	sixteenth[BEND_MAX];
} FREQ_SYNTH;



typedef struct {
	pid_t created_by;
	int samples;
	int rate;
	bool stereo;
} BEATS_HEADER;

typedef struct {
	char crap[4];
	int filesize;
	char morecrap[12];
	short format;
	short channels;
	int rate;
	char yetmorecrap[WAV_HEADER_SIZE-28];
} WAV_HEADER;

typedef struct {
	bool	synth_proceed;
	bool	reached_end;
	int	tones[3];
	int	bpm;
	int	bass_vol;
	int	drum_vol;	
	int		note_history_index;
	NOTE_HISTORY	note_history[NOTE_HISTORY_LIMIT];
} FRONT_INFO;




