/* ==========================================================================
 * Enlivener - A Time-Domain Live Guitar Tracker for Linux
 * File: enlivener-synth.c
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
#include <libgen.h>
#include <sys/mman.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include <time.h>
#include <fcntl.h>
#include <sched.h>
#include "enlivener.h" 
#include "pivotsort.h"



// frequencies used for output synthesis of bass tones
double musical_tones[]={
/*
20.60172, 21.82676, 23.12465, 24.49971, 25.95654, 27.50000,
29.13524, 30.86771, 32.70320, 34.64783, 36.70810, 38.89087,
*/
41.20344, 43.65353, 46.24930, 48.99943, 51.91309, 55.00000,
58.27047, 61.73541, 65.40639, 69.29566, 73.41619, 77.78175,

82.40689, 87.30706, 92.49861, 97.99886, 103.8262, 110.0000, // E-A
116.5409, 123.4708, 130.8128, 138.5913, 146.8324, 155.5635, // Bb-Eb
/*
164.8138, 174.6141, 184.9972, 195.9977, 207.6523, 220.0000, 
233.0819, 246.9417, 261.6256, 277.1826, 293.6648, 311.1270, 

329.6276, 349.2282, 369.9944, 391.9954, 415.3047, 440.0000, 
466.1638, 493.8833, 523.2511, 554.3653, 587.3295, 622.2540, 

659.2551, 698.4565, 739.9888, 783.9909, 830.6094, 880.0000, 
932.3275, 987.7666, 1046.502, 1108.731, 1174.659, 1244.508, 

1318.510, 1396.913, 1479.978, 1567.982, 1661.219, 1760.000, 
1864.655, 1975.533, 2093.005, 2217.461, 2349.318, 2489.016, 

2637.020, 2793.826, 2959.955, 3135.963, 3322.438, 3520.000, 
3729.310, 3951.066, 4186.009, 4434.922, 4698.636, 4978.032,

5274.041, 5587.652, 5919.911, 6271.927, 6644.875, 7040.000,
7458.620, 7902.133, 8372.018, 8869.844, 9397.273, 9956.063,
*/
 0 };

// Frequencies used for output synthesis of "tinkly sounds" or "icing"
double	icing_tones[] = { 
659.2551, 698.4565, 739.9888, 783.9909, 830.6094, 880.0000, 
932.3275, 987.7666, 1046.502, 1108.731, 1174.659, 1244.508, 

1318.510, 1396.913, 1479.978, 1567.982, 1661.219, 1760.000, 
1864.655, 1975.533, 2093.005, 2217.461, 2349.318, 2489.016,

2637.020, 2793.826, 2959.955, 3135.963, 3322.438, 3520.000, 
3729.310, 3951.066, 4186.009, 4434.922, 4698.636, 4978.032,

0
};


// The table below is where I worked out what the "key_mask" array of shorts
// would have to look like.  You can see where the binary number on the
// left represents the minor scale of the note indicated, where the leftmost
// bit represents E.  The actual number the computer uses is the one on the
// right, which is a left-to-right reversal of the left number, which is
// strictly for visualization.

// 101101011010  E  010110101101
// 010110101101  F  101101011010
// 101011010110 F#  011010110101
// 010101101011 G   110101101010
// 101010110101 G#  101011010101
// 110101011010 A  010110101011
// 011010101101 A#  101101010110
// 101101010110  B  011010101101
// 010110101011  C  110101011010
// 101011010101 C#  101010110101
// 110101101010  D  010101101011
// 011010110101 D#  101011010110

unsigned short	key_mask[] = {
	0x5ad,	// E
	0xb5a,	// F
	0x6b5,	// F#
	0xd6a,	// G
	0xad5,	// G#
	0x5ab,	// A
	0xb56,	// A#
	0x6ad,	// B
	0xd5a,	// C
	0xab5,	// C#
	0x56b,	// D
	0xad6	// D#
	};

// Miscellaneous necessary global objects

WAV_HEADER	dat_header;
bool		stereo;
bool		have_drums;

int		max_tone;
int		max_icing;

int		rate;

int		beat_index;
int		beat_limit;

long		last_play;

short		*collecter;

NOTE_HISTORY	note_history[NOTE_HISTORY_LIMIT];
int		note_history_index;

NOTE_ENTRY	**note_entry;

int		accents[7];
int		accents_count;

bool		quiet;
int		quiet_count;

short		*icing_overplay;
bool		icing_have_overplay;

int		bpm;

pa_simple	*pa_index_out; 
pa_sample_spec	ss_out;

KEY_INFO	*key_info;

FRONT_INFO	*front_info;

int		use_tones[3];

ICING		**icing;
FREQ_SYNTH	**freq;
short		*audio;
pid_t		basspid;
short		*beatbuf;
short		*mixbuf;
int		prev_tones[3];



// function declarations
void		set_icing();
void		set_note_entry();
void		play_something();
void 		read_drums();
void		humanize_vol();
void		make_waves();
void		mute_previous();
void		collect_waves();
void		pivotSort();
unsigned char	sort_note_entry();
void		find_key();
void		best_key_match();
void		do_icing();


// This function produces all the piano-y tinkling sounds
void do_icing(buf,ps_tones)
short	*buf;
int	*ps_tones;
{
int	c;			// a counter

int	sample_int;		// object for sample calculation.  Exists
				// so it can hold larger numbers than the
				// short type that actually goes into the
				// buffer, so I can have ceiling clipping
				// behavior rather than awful digital 
				// wraparound.

int	octave;			// Tone selection offset that is always 
				// zero or a mulitple of twelve, because
				// it's directly added to the granular
				// tone value

int	r_offset;		// Offsets for stereo "airy" effect.
int	l_offset;


bool	have_this[12];

int	ice_tones[12];
int	ice_tones_index;

int	this_ice;

int	first_tone;
int	second_tone;


	ice_tones_index=0;

//
// icing_have_overplay is a global bool that's set at the end of this function,
// and what it means it there's a little bit of audio overwrite from the last
// iteration that should have this mixed over top of it.  This is because
// of random phase shifting for tone reasons	
	
// The rest is as obvious as it appears: Get notes from the two available
// sources and write notes accordingly all over the buffer according to the
// logic driven by misc rand() calls.

	if(icing_have_overplay) {
		for(c=0;c<SINE_QUARTER*2;c++) {
			sample_int=buf[c*2];
			sample_int+=icing_overplay[c];
			if(sample_int>32767)sample_int=32767;
			if(sample_int<-32768)sample_int=-32768;
			buf[c*2]=sample_int;
			
			sample_int=buf[c*2];
			sample_int+=icing_overplay[c];
			if(sample_int>32767)sample_int=32767;
			if(sample_int<-32768)sample_int=-32768;
			buf[c*2]=sample_int;
		}
		icing_have_overplay=false;
	}
	for(c=0;c<12;c++) have_this[c]=false;
	
	for(c=0;c<accents_count;c++) {
		ice_tones[ice_tones_index]=accents[c];
		ice_tones_index++;
		have_this[c]=true;
	}
	
	for(c=0;c<3;c++) {
		if(ps_tones[c]==-1)continue;
		if(have_this[c])continue;
		ice_tones[ice_tones_index]=ps_tones[c];
		ice_tones_index++;
		have_this[c]=true;
	}
//}

	for(c=0;c<12;c++) have_this[c]=false;
	
	octave=0;
	
	if(key_info->triad) {
		octave=(rand()%2)*12;

		l_offset=(rand()%ICING_OFFSET);
		r_offset=(rand()%ICING_OFFSET);
		
		for(c=0;c<SINE_QUARTER*2;c++) {
			sample_int=buf[(c+l_offset)*2];
			sample_int+=icing[ps_tones[0]+octave]->half[c];
			
			if(sample_int>32767) sample_int=32767;
			if(sample_int<-32768) sample_int=-32768;
			
			buf[(c+l_offset)*2]=sample_int;
			
			sample_int=buf[((c+r_offset)*2)+1];
			sample_int+=icing[ps_tones[0]+octave]->half[c];
			
			if(sample_int>32767) sample_int=32767;
			if(sample_int<-32768) sample_int=-32768;
			
			buf[((c+r_offset)*2)+1]=sample_int;
		}
		l_offset=(rand()%ICING_OFFSET);
		r_offset=(rand()%ICING_OFFSET);
		octave=(rand()%2)*12;
		if(rand()%2)
		for(c=0;c<SINE_QUARTER*2;c++) {
			sample_int=buf[(c+l_offset)*2];
			sample_int+=icing[ps_tones[1]+octave]->half[c];
			
			if(sample_int>32767) sample_int=32767;
			if(sample_int<-32768) sample_int=-32768;
			
			buf[(c+l_offset)*2]=sample_int;
			
			sample_int=buf[((c+r_offset)*2)+1];
			sample_int+=icing[ps_tones[1]+octave]->half[c];
			
			if(sample_int>32767) sample_int=32767;
			if(sample_int<-32768) sample_int=-32768;
			
			buf[((c+r_offset)*2)+1]=sample_int;
		}
		l_offset=(rand()%ICING_OFFSET);
		r_offset=(rand()%ICING_OFFSET);
		octave=(rand()%2)*12;
		if(rand()%2)
		for(c=0;c<SINE_QUARTER*2;c++) {
			sample_int=buf[(c+l_offset)*2];
			sample_int+=icing[ps_tones[2]+octave]->half[c];
			
			if(sample_int>32767) sample_int=32767;
			if(sample_int<-32768) sample_int=-32768;
			
			buf[(c+l_offset)*2]=sample_int;
			
			sample_int=buf[((c+r_offset)*2)+1];
			sample_int+=icing[ps_tones[2]+octave]->half[c];
			
			if(sample_int>32767) sample_int=32767;
			if(sample_int<-32768) sample_int=-32768;
			
			buf[((c+r_offset)*2)+1]=sample_int;
		}
	} else {
this_ice=ice_tones[rand()%ice_tones_index];
switch(rand()%5) {

case 0:		
		octave=(rand()%2)*12;
		l_offset=(rand()%ICING_OFFSET);
		r_offset=(rand()%ICING_OFFSET);
		for(c=0;c<SINE_QUARTER;c++) {
			sample_int=buf[(c+l_offset)*2];
			sample_int+=icing[this_ice+octave]->quarter[c];
			
			if(sample_int>32767) sample_int=32767;
			if(sample_int<-32768) sample_int=-32768;
			
			buf[(c+l_offset)*2]=sample_int;
			
			sample_int=buf[((c+r_offset)*2)+1];
			sample_int+=icing[this_ice+octave]->quarter[c];
			
			if(sample_int>32767) sample_int=32767;
			if(sample_int<-32768) sample_int=-32768;
			
			buf[((c+r_offset)*2)+1]=sample_int;

		}
		break;
		
case 1:		octave=(rand()%2)*12;
		l_offset=(rand()%ICING_OFFSET);
		r_offset=(rand()%ICING_OFFSET);
		for(c=0;c<SINE_EIGHTH;c++) {
			sample_int=buf[(c+l_offset)*2];
			sample_int+=icing[this_ice+octave]->eighth[c];
			
			if(sample_int>32767) sample_int=32767;
			if(sample_int<-32768) sample_int=-32768;
			
			buf[(c+l_offset)*2]=sample_int;
			
			sample_int=buf[((c+r_offset)*2)+1];
			sample_int+=icing[this_ice+octave]->eighth[c];
			
			if(sample_int>32767) sample_int=32767;
			if(sample_int<-32768) sample_int=-32768;
			
			buf[((c+r_offset)*2)+1]=sample_int;

		}
		octave=(rand()%2)*12;
		l_offset=(rand()%ICING_OFFSET);
		r_offset=(rand()%ICING_OFFSET);
		for(c=0;c<SINE_EIGHTH;c++) {
			sample_int=buf[(SINE_EIGHTH+c+l_offset)*2];
			sample_int+=icing[this_ice+octave]->eighth[c];
			
			if(sample_int>32767) sample_int=32767;
			if(sample_int<-32768) sample_int=-32768;
			
			buf[(SINE_EIGHTH+c+l_offset)*2]=sample_int;
			
			sample_int=buf[((SINE_EIGHTH+c+r_offset)*2)+1];
			sample_int+=icing[this_ice+octave]->eighth[c];
			
			if(sample_int>32767) sample_int=32767;
			if(sample_int<-32768) sample_int=-32768;
			
			buf[((SINE_EIGHTH+c+r_offset)*2)+1]=sample_int;

		}
		break;
		
case 2: 	octave=(rand()%2)*12;
		l_offset=(rand()%ICING_OFFSET);
		r_offset=(rand()%ICING_OFFSET);

		for(c=0;c<SINE_EIGHTH;c++) {
			sample_int=buf[(c+l_offset)*2];
			sample_int+=icing[this_ice+octave]->eighth[c];
			
			if(sample_int>32767) sample_int=32767;
			if(sample_int<-32768) sample_int=-32768;
			
			buf[(c+l_offset)*2]=sample_int;
			
			sample_int=buf[((c+r_offset)*2)+1];
			sample_int+=icing[this_ice+octave]->eighth[c];
			
			if(sample_int>32767) sample_int=32767;
			if(sample_int<-32768) sample_int=-32768;
			
			buf[((c+r_offset)*2)+1]=sample_int;

		}
		break;
		
case 3:		octave=(rand()%2)*12;
		l_offset=(rand()%ICING_OFFSET);
		r_offset=(rand()%ICING_OFFSET);
		for(c=0;c<SINE_EIGHTH;c++) {
			sample_int=buf[(SINE_EIGHTH+c+l_offset)*2];
			sample_int+=icing[this_ice+octave]->eighth[c];
			
			if(sample_int>32767) sample_int=32767;
			if(sample_int<-32768) sample_int=-32768;
			
			buf[(SINE_EIGHTH+c+l_offset)*2]=sample_int;
			
			sample_int=buf[((SINE_EIGHTH+c+r_offset)*2)+1];
			sample_int+=icing[this_ice+octave]->eighth[c];
			
			if(sample_int>32767) sample_int=32767;
			if(sample_int<-32768) sample_int=-32768;
			
			buf[((SINE_EIGHTH+c+r_offset)*2)+1]=sample_int;

		}
		break;
		
case 4:		octave=(rand()%2)*12;
		l_offset=(rand()%ICING_OFFSET);
		r_offset=(rand()%ICING_OFFSET);
		icing_have_overplay=true;
		
		for(c=0;c<SINE_QUARTER*4;c++) {
			if(l_offset<SINE_QUARTER*2) {
				sample_int=buf[l_offset*2];
				sample_int+=icing[this_ice+octave]->whole[c];
				if(sample_int>32767) sample_int=32767;
				if(sample_int<-32768) sample_int=-32768;
				buf[l_offset*2]=sample_int;
			} else {
				if(l_offset<SINE_QUARTER*4)
				icing_overplay[(l_offset-(SINE_QUARTER*2))*2]
				=icing[this_ice+octave]->whole[c];	
			}	
			
			if(r_offset<SINE_QUARTER*2) {
				sample_int=buf[(r_offset*2)+1];
				sample_int+=icing[this_ice+octave]->whole[c];
				if(sample_int>32767) sample_int=32767;
				if(sample_int<-32768) sample_int=-32768;
				buf[(r_offset*2)+1]=sample_int;
			} else {
				if(r_offset<SINE_QUARTER*4)
				icing_overplay[((r_offset-(SINE_QUARTER*2))*2)+1]
				=icing[this_ice+octave]->whole[c];	
			}	
		}
}
	}
//	for(;;) {
		this_ice=ice_tones[rand()%ice_tones_index];
//		if(c!=this_ice) {
//			this_ice=c;
//			break;
//		}
//	}
	octave=(rand()%2)*12;
	l_offset=(rand()%ICING_OFFSET);
	r_offset=(rand()%ICING_OFFSET);
	for(c=0;c<SINE_EIGHTH;c++) {
		sample_int=buf[(SINE_QUARTER+c+l_offset)*2];
		sample_int+=icing[this_ice+octave]->eighth[c];
			
		if(sample_int>32767) sample_int=32767;
		if(sample_int<-32768) sample_int=-32768;
			
		buf[(SINE_QUARTER+c+l_offset)*2]=sample_int;
			
		sample_int=buf[((SINE_QUARTER+c+r_offset)*2)+1];
		sample_int+=icing[this_ice+octave]->eighth[c];
			
		if(sample_int>32767) sample_int=32767;
		if(sample_int<-32768) sample_int=-32768;
			
		buf[((SINE_QUARTER+c+r_offset)*2)+1]=sample_int;

	}
//	for(;;) {
		this_ice=ice_tones[rand()%ice_tones_index];
//		if(c!=this_ice) {
//			this_ice=c;
//			break;
//		}
//	}
	octave=(rand()%2)*12;
	l_offset=(rand()%ICING_OFFSET);
	r_offset=(rand()%ICING_OFFSET);
	for(c=0;c<SINE_EIGHTH;c++) {
		sample_int=buf[(SINE_QUARTER+SINE_EIGHTH+c-l_offset)*2];
		sample_int+=icing[this_ice+octave]->eighth[c];
			
		if(sample_int>32767) sample_int=32767;
		if(sample_int<-32768) sample_int=-32768;
			
		buf[(SINE_QUARTER+SINE_EIGHTH+c-l_offset)*2]=sample_int;
			
		sample_int=buf[((SINE_QUARTER+SINE_EIGHTH+c-r_offset)*2)+1];
		sample_int+=icing[this_ice+octave]->eighth[c];
			
		if(sample_int>32767) sample_int=32767;
		if(sample_int<-32768) sample_int=-32768;
			
		buf[((SINE_QUARTER+SINE_EIGHTH+c-r_offset)*2)+1]=sample_int;

	}
}

//
// This basically takes the a mask inherited by the calling function find_key()
// and tries to use that to select a key to use
void best_key_match(mask)
unsigned short mask;
{
int	c;
int	d;
int	conflict;
int	least_conflict;
int	least_conflict_index;
int	conflict_mark[12];

unsigned short	possible_keys_mask;
int		possible_keys_count;

unsigned short	common_tones_mask;
//int		common_tones_count;

	least_conflict=12;
	possible_keys_mask=0;
	possible_keys_count=0;
	accents_count=0;
	
	for(c=0;c<12;c++) {
		conflict=0;
		for(d=0;d<12;d++) {
		// About the following line, if(!(mask&(1<<d)))continue;
		// That might appear at first glance to be a mistake in the logic,
		// but it's a deliberate choice.  You see, the absence of a
		// note is not an assertion by the musician.  You can easily
		// have a piece of music that doesn't have all the notes in
		// its key. So, guitar&bit==0, machine&bit==bit is not a conflict,
		// but guitar&bit==bit, machine&bit==0 is.
			if(!(mask&(1<<d)))continue;
			if(key_mask[c]&(1<<d))continue;
			++conflict;
		}
		conflict_mark[c]=conflict;
		if(conflict<least_conflict) {
			least_conflict=conflict;
			least_conflict_index=c;
		}
	}

//	common_tones_count=0;
	common_tones_mask=0xfff;
		
	for(c=0;c<12;c++) if(conflict_mark[c]==least_conflict) {
		possible_keys_count++;
		possible_keys_mask|=(1<<c);
		common_tones_mask&=key_mask[c];
	}

	// This isn't guaranteed to be correct, but if p_k_c is 1, that means
	// for all practical purposes it has selected a key, so it might as
	// well tell me what it is.
	if(possible_keys_count==1) key_info->key_is=least_conflict_index;

	for(c=0;c<12;c++) if(common_tones_mask&(1<<c)) {
		accents[accents_count]=c;
		accents_count++;
	}
	
//	if(key_info!=MAP_FAILED) {
//		key_info->key_count=possible_keys_count;
		key_info->accents_count=accents_count;
		memcpy(key_info->accents,accents,sizeof(int)*accents_count);
//		key_info->common_mask=common_tones_mask;
//	}
//	return((least_conflict==12)?-1:least_conflict_index);
}

//
// helper function for pivotSort
unsigned char sort_note_entry(n1,n2)
NOTE_ENTRY	*n1;
NOTE_ENTRY	*n2;
{
	if((n1->weight)>(n2->weight))return(SORT_KEEP);
	if((n2->weight)>(n1->weight))return(SORT_SWAP);
	return(SORT_EQUAL);
}

// examine the recent notes on the harmony stack and come up with a key
// we think the guitarist is playing in, or, failing that, make a smaller list
// of notes consisting of the most recent harmony stack data
void find_key()
{
int	c;
int	d;
int	note_lookup;
int	importance;
//int	is_key;

unsigned short my_key_mask;

int	oldest_index;

	oldest_index=note_history_index;
	++oldest_index;
	if(oldest_index==NOTE_HISTORY_LIMIT)oldest_index=0;
	
	key_info->oldest_note=note_history[oldest_index].played_at;
	
	my_key_mask=0;
	accents_count=0;
	for(c=0;c<7;c++) accents[c]=-1;
		
//	is_key=-1;
	
	for(c=0;c<12;c++) {
		note_entry[c]->tone=c;
		note_entry[c]->weight=0;
	}

	importance=NOTE_HISTORY_LIMIT;
	note_lookup=note_history_index;
		
//	printf("i: %d - ",importance);
	
	for(;;) {
		if(note_history[note_lookup].tone!=-1) 
		(note_entry[note_history[note_lookup].tone]->weight)
		+=(importance*NOTE_WEIGHT_MULTIPLIER);
		
		if(note_lookup>0)--note_lookup;
		else note_lookup=NOTE_HISTORY_LIMIT-1;
		
		if(note_lookup==note_history_index)break;
		
		--importance;
	}
	
//	printf(" - i: %d  - ",importance);
	
	pivotSort(note_entry,12,sort_note_entry);
	
//	putchar('\n');
	
//	for(c=0;c<7;c++) if(note_entry[c]->weight>0)
//	printf("%d ",note_entry[c]->tone/*,note_entry[c]->weight*/);
	
//	putchar('\n');
	
	for(c=0;c<7;c++) {
		if(note_entry[c]->weight==0)break;
		my_key_mask|=(1<<(note_entry[c]->tone));
	}

//	printf(" - mask: %x\n",my_key_mask);
		
	for(c=0;c<12;c++) if(key_mask[c]==my_key_mask) {
//		is_key=c;
		// What luck!
//		key_info->key_count=1;

//		We don't need the notes if a key is decided, because the front
//		just displays the key in that case
//		key_info->accents_count;=7;

		key_info->key_is=c;		
		accents_count=0;
		for(d=0;d<12;d++) if(key_mask[c]&(1<<d)) {
			accents[accents_count]=d;
			accents_count++;
		}
		
		return;
	}

	key_info->key_is=-1;
	
	best_key_match(my_key_mask);
		
//	return(is_key);
}

// I knew why this months ago when I wrote it but I don't now.  (Oct 2026)
void mute_previous(buf)
short *buf;
{
int	count;
short	*fade_at;
int	c;
int	sample_int;
int	mute_index;

	if(/*(*buf==0)||*/(buf==audio)) return;

	mute_index=(rand()%(MUTE_PREVIOUS-MUTE_PREVIOUS_MIN))+MUTE_PREVIOUS_MIN;
		
	fade_at=buf;
	count=0;
	
	while((fade_at>audio)&&(count<mute_index)) {
		--fade_at;
		--fade_at;
		++count;
	}
	
	for(c=0;c<count;c++) {
		sample_int=fade_at[c*2];
		sample_int*=(count-c);
		sample_int/=count;
		fade_at[c*2]=(short)sample_int;

		sample_int=fade_at[(c*2)+1];
		sample_int*=(count-c);
		sample_int/=count;
		fade_at[(c*2)+1]=(short)sample_int;
	}
}


//
// Introduce random microvariations in the volume to sound less mechanical
//
// Oh yeah, now I remember, and plus some other tonal adjustments as well.
void humanize_vol(set,buf,count)
TRANSVERSE	*set;
short		*buf;
int		count;
{
int	vol;
 int	this_vol;
int	c;
int	fade;
int	fade_pos;
int	sample_int_l,sample_int_r;
int	attack;
int	this_attack;
int	attack_len;
int	bend_index;

int	t_val;

int	oct_variance_l;
int	third_variance_l;
int	fourth_variance_l;
int	fifth_variance_l;

int	oct_variance_r;
int	third_variance_r;
int	fourth_variance_r;
int	fifth_variance_r;

int	oct_delay_l;
int	oct_delay_r;
int	third_delay_l;
int	third_delay_r;
int	fourth_delay_l;
int	fourth_delay_r;
int	fifth_delay_l;
int	fifth_delay_r;

bool	harmonic;
int	harmonic_adjust;

int	mute_early;

	oct_delay_l=(rand()%MAX_TRANS_DELAY);
	oct_delay_r=(rand()%MAX_TRANS_DELAY);

	third_delay_l=(rand()%MAX_TRANS_DELAY);
	third_delay_r=(rand()%MAX_TRANS_DELAY);

	fourth_delay_l=(rand()%MAX_TRANS_DELAY);
	fourth_delay_r=(rand()%MAX_TRANS_DELAY);

	fifth_delay_l=(rand()%MAX_TRANS_DELAY);
	fifth_delay_r=(rand()%MAX_TRANS_DELAY);	
	
	mute_early=(rand()%MUTE_EARLY);
	
	harmonic=false;
	
	mute_previous(buf);
		
	if(rand()%2)bend_index=0;
	else bend_index=(rand()%(BEND_MAX-1))+1;
			
	set=&(set[bend_index]);
	
	vol=(rand()%HUMANIZE_VOL)+(32767-HUMANIZE_VOL);
	if(count==SINE_SIXTEENTH)vol=(vol*3)/5;

	
	fade=(rand()%
	  (((count>BASS_FADE)?BASS_FADE:count)       -BASS_FADE_FLOOR))
	+BASS_FADE_FLOOR;

	while(fade>count)fade/=2;

		
	attack=(rand()%(BASS_ATTACK-BASS_ATTACK_MIN))+BASS_ATTACK_MIN;

	attack_len=(BASS_ATTACK_LEN>(count-fade))?(count-fade):BASS_ATTACK_LEN;		

oct_variance_l=(rand()%(TRANSVERSE_VARIANCE-TRANS_VARIANCE_MIN)+TRANS_VARIANCE_MIN);

third_variance_l=(rand()%(TRANSVERSE_VARIANCE-TRANS_VARIANCE_MIN)+TRANS_VARIANCE_MIN);

fourth_variance_l=(rand()%(TRANSVERSE_VARIANCE-TRANS_VARIANCE_MIN)+TRANS_VARIANCE_MIN);

fifth_variance_l=(rand()%(TRANSVERSE_VARIANCE-TRANS_VARIANCE_MIN)+TRANS_VARIANCE_MIN);

oct_variance_r=(rand()%(TRANSVERSE_VARIANCE-TRANS_VARIANCE_MIN)+TRANS_VARIANCE_MIN);

third_variance_r=(rand()%(TRANSVERSE_VARIANCE-TRANS_VARIANCE_MIN)+TRANS_VARIANCE_MIN);

fourth_variance_r=(rand()%(TRANSVERSE_VARIANCE-TRANS_VARIANCE_MIN)+TRANS_VARIANCE_MIN);

fifth_variance_r=(rand()%(TRANSVERSE_VARIANCE-TRANS_VARIANCE_MIN)+TRANS_VARIANCE_MIN);

	
	if(count==SINE_EIGHTH) if((rand()%2)==0) {
		harmonic=true;
		harmonic_adjust=(rand()%(BASS_PEAK-HARMONIC_FLOOR))+HARMONIC_FLOOR;
	}
	
	for(c=0;c<count;c++) {

		this_vol=vol;

		sample_int_l=set->fundamental[c];
		sample_int_r=set->fundamental[c];
		
		if(harmonic) {
			sample_int_l*=harmonic_adjust;
			sample_int_l/=BASS_PEAK;

			sample_int_r*=harmonic_adjust;
			sample_int_r/=BASS_PEAK;
		}
				
	t_val=set->octave[((c+oct_delay_l)<count)?(c+oct_delay_l):(count-1)];
	
		t_val*=oct_variance_l;
		t_val/=TRANSVERSE_VARIANCE;
		
		if(harmonic) {
			t_val*=BASS_PEAK;
			t_val/=harmonic_adjust;
		}
		
		sample_int_l+=t_val;


		
	t_val=set->octave[((c+oct_delay_r)<count)?(c+oct_delay_r):(count-1)];
		t_val*=oct_variance_r;
		t_val/=TRANSVERSE_VARIANCE;
		
		if(harmonic) {
			t_val*=BASS_PEAK;
			t_val/=harmonic_adjust;
		}
		
		sample_int_r+=t_val;
		


				
	t_val=set->third[((c+third_delay_l)<count)?(c+third_delay_l):(count-1)];
		
		t_val*=third_variance_l;
		t_val/=TRANSVERSE_VARIANCE;

		if(harmonic) {
			t_val*=BASS_PEAK;
			t_val/=harmonic_adjust;
		}

		sample_int_l+=t_val;

	t_val=set->third[((c+third_delay_r)<count)?(c+third_delay_r):(count-1)];
		t_val*=third_variance_r;
		t_val/=TRANSVERSE_VARIANCE;

		if(harmonic) {
			t_val*=BASS_PEAK;
			t_val/=harmonic_adjust;
		}

		sample_int_r+=t_val;
		



	t_val=set->fourth[((c+fourth_delay_l)<count)?(c+fourth_delay_l):(count-1)];
		
		t_val*=fourth_variance_l;
		t_val/=TRANSVERSE_VARIANCE;

		if(harmonic) {
			t_val*=BASS_PEAK;
			t_val/=harmonic_adjust;
		}
		sample_int_l+=t_val;


	t_val=set->fourth[((c+fourth_delay_r)<count)?(c+fourth_delay_r):(count-1)];
		t_val*=fourth_variance_r;
		t_val/=TRANSVERSE_VARIANCE;

		if(harmonic) {
			t_val*=BASS_PEAK;
			t_val/=harmonic_adjust;
		}
		sample_int_r+=t_val;
		


		
	t_val=set->fifth[((c+fifth_delay_l)<count)?(c+fifth_delay_l):(count-1)];
		t_val*=fifth_variance_l;
		t_val/=TRANSVERSE_VARIANCE;

		if(harmonic) {
			t_val*=BASS_PEAK;
			t_val/=harmonic_adjust;
		}
		
		sample_int_l+=t_val;
		
		
	t_val=set->fifth[((c+fifth_delay_r)<count)?(c+fifth_delay_r):(count-1)];
		t_val*=fifth_variance_r;
		t_val/=TRANSVERSE_VARIANCE;

		if(harmonic) {
			t_val*=BASS_PEAK;
			t_val/=harmonic_adjust;
		}
		
		sample_int_r+=t_val;
		
	

		if(c>count-(fade+mute_early)) {
			if(c<count-mute_early) {
				fade_pos=(count-mute_early)-c;
				this_vol*=fade_pos;
				this_vol/=fade;		
			} else this_vol=0;
		}

				
		sample_int_l=(sample_int_l*this_vol)/32767;

		if(sample_int_l>BASS_PEAK)sample_int_l=BASS_PEAK;
		if(sample_int_l<(BASS_PEAK*-1)) sample_int_l=(BASS_PEAK*-1);

		sample_int_r=(sample_int_r*this_vol)/32767;

		if(sample_int_r>BASS_PEAK)sample_int_r=BASS_PEAK;
		if(sample_int_r<(BASS_PEAK*-1)) sample_int_r=(BASS_PEAK*-1);
		
		
		sample_int_l*=(front_info->bass_vol);
		sample_int_l/=FRONT_BASS_VOLUME;
		
		sample_int_r*=(front_info->bass_vol);
		sample_int_r/=FRONT_BASS_VOLUME;
		
		if(sample_int_l>32767) sample_int_l=32767;
		if(sample_int_l<-32768) sample_int_l=-32768;
		
		if(sample_int_r>32767) sample_int_r=32767;
		if(sample_int_r<-32768) sample_int_r=-32768;
		
		buf[c*2]=sample_int_l;
		buf[(c*2)+1]=sample_int_r;
	}
	
	for(c=0;c<(attack_len/4);c++) {
		sample_int_l=buf[c*2];
		sample_int_l*=c;
		sample_int_l/=(attack_len/4);
		buf[c*2]=sample_int_l;
		
		sample_int_r=buf[(c*2)+1];
		sample_int_r*=c;
		sample_int_r/=(attack_len/4);
		buf[c*2]=sample_int_r;
	}

}
	
// Read the drums from the app-spec audio file in /tmp
void read_drums()
{
int c;
int dat_fd;
BEATS_HEADER dat_header;
char	drumname[256];

	bzero(drumname,256);
	sprintf(drumname,DRUM_FMT,geteuid());
	
	
	have_drums=false;
	
	dat_fd=open(drumname,O_RDONLY);
	
	if(dat_fd!=-1) {
		c=read(dat_fd,&dat_header,sizeof(BEATS_HEADER));
		if((c==sizeof(BEATS_HEADER))&&(dat_header.created_by==basspid))
			stereo=dat_header.stereo;
			rate=dat_header.rate;
	} else {
		have_drums=false;
		printf("synth: Can't open drums file!\n");
		return;
	}

	if((c==sizeof(BEATS_HEADER))&&(dat_header.created_by==basspid)) {

		beatbuf=malloc(dat_header.samples*
		sizeof(short)*(stereo?2:1));
		bzero(beatbuf,dat_header.samples*sizeof(short)*(stereo?2:1));

		
mixbuf=malloc((AUDIO_READ*2)*sizeof(short)*(stereo?2:1));
		
		beat_index=0;
		beat_limit=dat_header.samples;
			
	c=read(dat_fd,beatbuf,(dat_header.samples)*sizeof(short)*(stereo?2:1));

		
	} else printf("synth: Could not read drums file header\n");
	if(c==dat_header.samples*sizeof(short)*(stereo?2:1)) {
		have_drums=true;
	}  else {
		printf("synth: size mismatch: no drums %ld %d\n",
			dat_header.samples*sizeof(short)*(stereo?2:1),
			c);
		stereo=false;
		
		have_drums=false;
		free(beatbuf);
		beatbuf=NULL;
	}
	close(dat_fd);
}

// Invoked by main() in the ongoing execution loop to write a half bar
// and output it to the audio device
void play_something(tones)
int	*tones;
{
struct timespec	tp;
int		pa_says,pa_err;
short		*playbuf;
int		sample_int;

int adv_beat_index;

int c,d;


long	advance_samples;

short	*human_time;

int	use_tone;

int	fade_level_start;
int	fade_level_end;

int	actual_level;

bool	found_min_third;
bool	found_maj_third;
bool	found_fifth;

bool	used_fifth;
bool	used_third;


	clock_gettime(CLOCK_MONOTONIC,&tp);
				
	srand(time(&(tp.tv_nsec)));
		
	if(front_info->reached_end) {
		quiet_count++;
		if(quiet_count==QUIET_ITER) {
			kill(basspid,SIGUSR1);
			exit(EXIT_SUCCESS);
		}
	}	
	
	
	// This used to be needed to try and sync up a previous, network
	// packet synchronous version of the app.  It might still sometimes
	// execute under heavy load.  Since I care about the music and
	// not the execution details I didn't test the latter after the 
	// former was good.
	if((last_play!=-1)&&have_drums) {

				
		clock_gettime(CLOCK_MONOTONIC,&tp);
		
		advance_samples=(rate*(NANONANO-last_play))/1000000000;

		if(advance_samples>(SINE_QUARTER*2))
		printf("\n%ld iterations late!\n",
		advance_samples/(SINE_QUARTER*2));
		
		adv_beat_index=beat_index-(int)advance_samples;

		if(adv_beat_index<0)adv_beat_index=beat_limit-abs(adv_beat_index);		
		
if(tones[0]!=-1) {		
		for(c=0;c<(int)advance_samples;c++) {
			mixbuf[c*2]=quiet?0:(beatbuf[adv_beat_index*2]/2);
			mixbuf[(c*2)+1]=quiet?0:(beatbuf[(adv_beat_index*2)+1]/2);
			// not coding for mono anymore
			adv_beat_index++;
			if(adv_beat_index==beat_limit)adv_beat_index=0;
		}
} else bzero(mixbuf,AUDIO_READ*4*sizeof(short));
		
	
		pa_simple_write(pa_index_out,mixbuf,
		advance_samples*2*sizeof(short),&pa_err);
		
		advance_samples=advance_samples%(SINE_QUARTER*2);
		
	} else advance_samples=0;
	

	bzero(audio,(AUDIO_READ+HUMANIZE_TIME)*4*sizeof(short));

	note_history_index=front_info->note_history_index;
	memcpy(note_history,front_info->note_history,NOTE_HISTORY_LIMIT*
	sizeof(NOTE_HISTORY));
	
	find_key();


// this if means, if we're in regular play or mid fade out, 
// rather than in a silent period
if(

(tones[0]!=-1)||
(
(((quiet_count>0)&&(quiet_count<QUIET_ITER))&&(prev_tones[0]!=-1))
)) {


	if(quiet)beat_index=0;
	quiet=false;
	if(tones[0]!=-1) {
		if(!(front_info->reached_end))quiet_count=0;
		memcpy(prev_tones,tones,3*sizeof(int));
	} else {
		tones=prev_tones;
		if((quiet_count<QUIET_ITER)&&(!(front_info->reached_end)))
		quiet_count++;
		
		
		if((quiet_count==QUIET_ITER)&&(!(front_info->reached_end)))
		quiet=true;
	}

//
// Collect all the available notes that are good for us to use 
if(accents_count>0) {
	for(c=0;c<accents_count;c++) if(accents[c]==tones[0]) {
		for(d=c;d<accents_count-1;d++) accents[d]=accents[d+1];
		accents_count--;
		break;
	}
}

found_min_third=false;
found_maj_third=false;
found_fifth=false;

used_fifth=false;
used_third=false;

key_info->triad=false;

if(accents_count>0) {
	for(c=0;c<accents_count;c++) if(accents[c]==((tones[0]+3)%12)) {
		found_min_third=true;
		for(d=c;d<accents_count-1;d++) accents[d]=accents[d+1];
		accents_count--;
		break;
	}
}

if(accents_count>0) {
	for(c=0;c<accents_count;c++) if(accents[c]==((tones[0]+4)%12)) {
		found_maj_third=true;
		for(d=c;d<accents_count-1;d++) accents[d]=accents[d+1];
		accents_count--;
		break;
	}
}

if(accents_count>0) {
	for(c=0;c<accents_count;c++) if(accents[c]==((tones[0]+7)%12)) {
		found_fifth=true;
		for(d=c;d<accents_count-1;d++) accents[d]=accents[d+1];
		accents_count--;
		break;
	}
}

if((tones[1]==((tones[0]+7)%12))||(tones[2]==((tones[0]+7)%12))) {
	found_fifth=false;
	used_fifth=true;
}

if(tones[1]==-1) {
	if(found_fifth) {
		tones[1]=((tones[0]+7)%12);
		found_fifth=false;
		used_fifth=true;
	} else if(found_min_third) {
		tones[1]=((tones[0]+3)%12);
		found_min_third=false;
		used_third=true;
	} else if(found_maj_third&&(!used_third)) {
		tones[1]=((tones[0]+4)%12);
		found_maj_third=false;
		used_third=true;
	}
}

if(tones[2]==-1) {
	if(found_fifth) {
		tones[2]=((tones[0]+7)%12);
		found_fifth=false;
		used_fifth=true;
	} else if(found_min_third&&(!used_third)) {
		tones[2]=((tones[0]+3)%12);
		found_min_third=false;
		used_third=true;
	} else if(found_maj_third&&(!used_third)) {
		tones[2]=((tones[0]+4)%12);
		found_maj_third=false;
		used_third=true;
	}
}

if((accents_count>0)&&(tones[1]==-1)) {
	c=(rand()%accents_count);
	tones[1]=accents[c];
	for(d=c;d<accents_count-1;d++) accents[d]=accents[d+1];
	--accents_count;
}

if((accents_count>0)&&(tones[2]==-1)) {
	c=(rand()%accents_count);
	tones[2]=accents[c];
//	for(d=c;d<accents_count-1;d++) accents[d]=accents[d+1];
//	--accents_count;
}


if((tones[1]==(tones[0]+5)%12)&&found_maj_third) tones[1]=((tones[0]+4)%12);
if((tones[2]==(tones[0]+5)%12)&&found_maj_third) tones[2]=((tones[0]+4)%12);

if((tones[1]==(tones[0]+5)%12)&&found_min_third) tones[1]=((tones[0]+3)%12);
if((tones[2]==(tones[0]+5)%12)&&found_min_third) tones[2]=((tones[0]+3)%12);

key_info->triad=(used_third&&used_fifth);

	if(tones[1]==-1)tones[1]=tones[0];
	if(tones[2]==-1)tones[2]=tones[1];
	
	memcpy(key_info->tones,tones,3*sizeof(int));
	
//
// Now a rand tree similar in princple to the do_icing() one

if((rand()%3)>0) {		// bar overall type 1

	// About "human_time": yes, doing this means that the tail end of notes
	// will often be copied over by the beginning of the next note.
	// This is fine as far as I'm concerned.
	//
	// "human time" in this context means the single-digits millisecond
	// variations in time that notes will have because human brains and
	// muscles aren't that mechanically accurate, not difference in time
	// that a good bass player might deliberately introduce for musical
	// reasons.  There's only so much we can do here.  It is not a human,
	// after all.
	//
	// humanize_vol, same idea.
	if(rand()%2) {		// bar type 1-1
	
		human_time=&(audio[(rand()%HUMANIZE_TIME)*2]);


// continue to modify according to following line:
		humanize_vol(freq[tones[0]]->quarter,human_time,SINE_QUARTER);
	} else {		// bar type 1-2
	
		human_time=&(audio[(rand()%HUMANIZE_TIME)*2]);
		humanize_vol(freq[tones[1]]->eighth,human_time,SINE_EIGHTH);
		
	 	if((rand()%4)>0) {
	 	
	 	human_time=&(audio[(SINE_EIGHTH+(rand()%HUMANIZE_TIME))*2]);
		humanize_vol(freq[tones[0]]->eighth,human_time,SINE_EIGHTH);
		
		}
		
	}
	
} else {			// bar overall type 2

	if((rand()%4)>0) {	// bar type 2-1

		human_time=&(audio[(rand()%HUMANIZE_TIME)*2]);

		humanize_vol(freq[tones[0]]->quarter,human_time,SINE_QUARTER);
		
		if(rand()%2) {
		

		use_tone=tones[1]+(rand()%6)?0:12;
		human_time=(&(audio[(SINE_QUARTER+(rand()%HUMANIZE_TIME))*2]));
		
		humanize_vol(freq[use_tone]->eighth,human_time,SINE_EIGHTH);
		
		}
		
		if((rand()%3)>0) {
		
		use_tone=tones[(rand()%2)?0:2]+(rand()%6)?0:12;
		
		human_time=(&(audio[(SINE_QUARTER+
		+SINE_EIGHTH+(rand()%HUMANIZE_TIME))*2]));
		
		humanize_vol(freq[use_tone]->eighth,human_time,SINE_EIGHTH);
		
		}
		
	} else {		// bar type 2-2
		
		human_time=&(audio[((rand()%HUMANIZE_TIME))*2]);

		humanize_vol(freq[tones[1]]->eighth,human_time,SINE_EIGHTH);
		
		if((rand()%4)>0) {
		
		use_tone=tones[(rand()%2)?0:2]+(rand()%6)?12:0;
		human_time=(&(audio[(SINE_EIGHTH+(rand()%HUMANIZE_TIME))*2]));
	
		humanize_vol(freq[use_tone]->eighth,human_time,SINE_EIGHTH);
		
		}
		
		use_tone=tones[0]+(rand()%8)?12:0;
		human_time=(&(audio[(SINE_QUARTER+(rand()%HUMANIZE_TIME))*2]));
	
		humanize_vol(freq[use_tone]->sixteenth,human_time,SINE_SIXTEENTH);
		
		if(rand()%2) {
		if(rand()%2) {
		
		use_tone=tones[0]+(rand()%8)?12:0;
		
	human_time=(&(audio[(SINE_QUARTER+SINE_SIXTEENTH+(rand()%HUMANIZE_TIME))*2]));
		
			humanize_vol(freq[use_tone]->sixteenth,human_time,
			SINE_SIXTEENTH);
		}
		}
		
		use_tone=tones[2]+(rand()%8)?12:0;
	human_time=(&(audio[(SINE_QUARTER+SINE_EIGHTH+(rand()%HUMANIZE_TIME))*2]));
		
		humanize_vol(freq[use_tone]->sixteenth,human_time,
		SINE_SIXTEENTH);
		
		if(rand()%2)
		
		//
		// This one deliberately not time-humanized - sixteenth notes
		// are tiny enough already and I don't want the last one
		// getting pushed partway off the end of the half-bar
		use_tone=tones[2]+(rand()%8)?12:0;	
		
		humanize_vol(freq[use_tone]->sixteenth,
		&(audio[(SINE_QUARTER+SINE_EIGHTH+SINE_SIXTEENTH)*2]),
		SINE_SIXTEENTH);
		
	}
}
do_icing(audio,tones);
mute_previous(&(audio[SINE_QUARTER*4]));
			

} else {
	if(!(front_info->reached_end)) {	
		if(quiet_count<QUIET_ITER) quiet_count++;
		if(quiet_count==QUIET_ITER) {
			quiet=true;
			bzero(audio,(AUDIO_READ+HUMANIZE_TIME)*4*sizeof(short));
		}
	}
}

if(quiet)bzero(audio,(AUDIO_READ+HUMANIZE_TIME)*4*sizeof(short));

// if have drums, mix them in	
if(have_drums) {

	playbuf=mixbuf;



		
		for(c=0;c<SINE_QUARTER*2;c++) {

			
			sample_int=beatbuf[beat_index*2];
			
			sample_int*=(front_info->drum_vol);
			sample_int/=FRONT_DRUM_VOLUME;
									
			sample_int+=audio[c*2];
			
			if(sample_int>32767)sample_int=32767;
			if(sample_int<-32768)sample_int=-32768;
			
			mixbuf[c*2]=sample_int;
			
			
			sample_int=beatbuf[(beat_index*2)+1];

			sample_int*=(front_info->drum_vol);
			sample_int/=FRONT_DRUM_VOLUME;
								
			sample_int+=audio[(c*2)+1];
			
			if(sample_int>32767)sample_int=32767;
			if(sample_int<-32768)sample_int=-32768;
			
			mixbuf[(c*2)+1]=sample_int;	
			
			++beat_index;
			if(beat_index==beat_limit)beat_index=0;
				
		}

} else playbuf=audio; // This else means we're in silent mode.

// if fading out...
if(quiet_count>0) {

	fade_level_end=32767-((32767/QUIET_ITER)*(quiet_count+1));
	fade_level_start=fade_level_end+(32767/QUIET_ITER);
	
	actual_level=fade_level_start;


	// this data doesn't need to be stereo-treated even though it is stereo	
	for(c=0;c<(SINE_QUARTER*4);c++) {
		
		sample_int=playbuf[c];
		
		sample_int*=actual_level;
		sample_int/=32767;

		playbuf[c]=sample_int;
				
		if((c>0)&&((c%(32767-QUIET_ITER))==0))actual_level--;
	}
		
}

			
// write to audio device
pa_simple_write(pa_index_out,quiet?audio:playbuf,
((SINE_QUARTER*2))*sizeof(short)
*(stereo?2:1),&pa_err);

	// Store the time for a the skip check at the start of this function
	clock_gettime(CLOCK_MONOTONIC,&tp);
	last_play=NANONANO;


}

//
// Nothing real hard to figure out here.

int main(argc,argv)
int argc;
char *argv[];
{



struct timespec tp;


int	c;
int	d;

int	pa_says,pa_err;

long	main_last_play;
long	main_wait_time;

pid_t		synthpid;
in_port_t	not_important;
pid_t		old_synth;

char		portname[MISC_TEXT];
int		dat_fd;

int		shm_fd;

pid_t		player_pid;

struct sigaction	sa;

double			progress;

	c=sigaction(SIGCHLD,&sa,NULL);

	bzero(portname,MISC_TEXT);
	sprintf(portname,SHM_FRONT_FMT,geteuid());
	
	shm_fd=shm_open(portname,O_RDONLY,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	
	if(shm_fd==-1) {
		fprintf(stderr,"synth: failed to open shm fd for read\n");
		exit(EXIT_FAILURE);
	} else {
		
	front_info=mmap(NULL,sizeof(FRONT_INFO),PROT_READ,MAP_SHARED,shm_fd,0);
		if(front_info==MAP_FAILED) {
			fprintf(stderr,"synth: cant mmap\n");
			exit(EXIT_FAILURE);
		}
	}
	

	close(shm_fd);
	rate=48000;
	have_drums=false;
	stereo=false;
	last_play=-1;
	beatbuf=NULL;
	quiet=true;
	quiet_count=0;
	icing_have_overplay=false;
	
	prev_tones[0]=-1;
	prev_tones[1]=-1;
	prev_tones[2]=-1;



	bpm=front_info->bpm;
		

	basspid=getppid();

	synthpid=getpid();
	
	bzero(portname,MISC_TEXT);
	
	sprintf(portname,SHM_SYNTH_FMT,geteuid());
	
	shm_fd=shm_open(portname,O_RDWR|O_CREAT|O_TRUNC,
	S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	
	if(shm_fd==-1) fprintf(stderr,"synth: Can't open shared memory for front\n");
	else {
		ftruncate(shm_fd,sizeof(KEY_INFO));
		key_info=mmap(NULL,sizeof(KEY_INFO),PROT_READ|PROT_WRITE,MAP_SHARED,
			shm_fd,0);
		if(key_info==MAP_FAILED) fprintf(stderr,"synth: mmap to front failed\n");
	}
	
	close(shm_fd);
	
	if(key_info!=MAP_FAILED) {
		bzero(key_info,sizeof(KEY_INFO));
		key_info->tones[0]=-1;
		key_info->tones[1]=-1;
		key_info->tones[2]=-1;
		key_info->oldest_note=0;
	}
	
ss_out.format = PA_SAMPLE_S16NE;
ss_out.channels = /* (stereo?2:1); */ 2; // That's TWO.  Not "well maybe only one"
ss_out.rate = rate;

stereo=true;


pa_index_out = pa_simple_new(NULL,               // Use the default server.
                  "Enlivener Synth",	      // Our application's name.
                  PA_STREAM_PLAYBACK,
                  NULL,               // Use the default device.
                  "Auto Guitar Backing",   // Description of our stream.
                  &ss_out,                // Our sample format.
                  NULL,               // Use default channel map
                  NULL,               // Use default buffering attributes.
                  &pa_err               // Ignore error code.
                  );
              
	if(pa_index_out==NULL) {
		printf("audio driver failure\n");
		exit(EXIT_FAILURE);
	}


	// This is because it has to be pivotSort()ed
	note_entry=malloc(12*sizeof(NOTE_ENTRY *));
	for(c=0;c<12;c++) note_entry[c]=malloc(sizeof(NOTE_ENTRY));
	
	clock_gettime(CLOCK_MONOTONIC,&tp);

	note_history_index=0;
	for(c=0;c<NOTE_HISTORY_LIMIT;c++) {
		note_history[c].tone=-1;
		note_history[c].played_at=NANONANO;
	}






	for(max_tone=0;musical_tones[max_tone]!=0;max_tone++);
	for(max_icing=0;icing_tones[max_icing]!=0;max_icing++);
	
	freq=malloc(max_tone*sizeof(FREQ_SYNTH *));
	icing=malloc(max_icing*sizeof(ICING *));
	
	collecter=malloc(SINE_QUARTER*8*sizeof(short));
	if(collecter==NULL) {
		printf("Failed to malloc\n");
		exit(EXIT_FAILURE);
	}
	
	putchar('\n');
	
	
	for(c=0;c<max_tone;c++) {
		progress=(100/(double)max_tone)*c;
		printf("\rGenerating bass synth notes for %d bpm: %.2lf%%",
		bpm,progress);
		fflush(stdout);
		freq[c]=malloc(sizeof(FREQ_SYNTH));
		set_note_entry(c);
	}


	printf("\rGenerating bass synth notes for %d bpm: 100%%        \n",bpm);
	
	for(c=0;c<max_icing;c++) {
		icing[c]=malloc(sizeof(ICING));
		set_icing(c);
	}	


	read_drums();
		
	if(have_drums&&(!stereo)) {
		short *mono_away;
	
		mono_away=malloc(beat_limit*sizeof(short)*2);
		
		for(c=0;c<beat_limit;c++) {
			mono_away[c*2]=beatbuf[c];
			mono_away[(c*2)+1]=beatbuf[c];
		}
		free(beatbuf);
		beatbuf=mono_away;
		stereo=true; 	
	}
	
	
	
	audio=malloc((AUDIO_READ+HUMANIZE_TIME)*4*sizeof(short));
	
	// *2 for half not quarter, *2 for stereo
	// half because the overplay only is half, not whole
	icing_overplay=malloc(SINE_QUARTER*4*sizeof(short));

	kill(basspid,SIGUSR1);
	

main_wait_time=((SINE_QUARTER*2*1000000000)/rate)-MAIN_PROC_ALLOW;

for(;;) {
	while(!(front_info->synth_proceed));
	
	if(kill(basspid,0)==-1) {
		printf("synth: enlivener no longer active\n");
		exit(EXIT_SUCCESS);
	}
	
	clock_gettime(CLOCK_MONOTONIC,&tp);
	main_last_play=NANONANO;
	
	memcpy(use_tones,front_info->tones,3*sizeof(int));
	play_something(use_tones);
	
	for(;;) {
		clock_gettime(CLOCK_MONOTONIC,&tp);
		if(NANONANO>(main_last_play+main_wait_time)) break;

		sched_yield();

	}
}
}	

///////////////////////	


// The rest of it is all wave pre-synthesis stuff except a pivot sorter
// at the end

void set_icing(tone)
int	tone;
{
double	frequency;
double	period;

double	angle;
double	my_sin;
double	step;

int	index;

double	fade_val;

double	d_sample;

	frequency=icing_tones[tone];
	period=rate/frequency;

	step=(2*M_PI)/period;

	icing[tone]->whole=malloc(SINE_QUARTER*4*sizeof(short));	
	icing[tone]->half=malloc(SINE_QUARTER*2*sizeof(short));
	icing[tone]->quarter=malloc(SINE_QUARTER*sizeof(short));
	icing[tone]->eighth=malloc(SINE_EIGHTH*sizeof(short));
	
	angle=0;
	
	for(index=0;index<SINE_QUARTER*4;index++) {
		my_sin=cos(angle);
		
		d_sample=my_sin*ICING_LEVEL;
	
		fade_val=d_sample*((double)((SINE_QUARTER*4)-index));
		fade_val/=(double)(SINE_QUARTER*4);
		
		icing[tone]->whole[index]=(short)fade_val;
		
		
		if(index<SINE_QUARTER*2) {	
			fade_val=d_sample*((double)((SINE_QUARTER*2)-index));
			fade_val/=(double)(SINE_QUARTER*2);
		
			icing[tone]->half[index]=(short)fade_val;
		}
		
		if(index<SINE_QUARTER) {
		
			fade_val=d_sample*((double)((SINE_QUARTER)-index));
			fade_val/=(double)(SINE_QUARTER);
			
			icing[tone]->quarter[index]=(short)fade_val;
		}
		
		if(index<SINE_EIGHTH) {
		
			fade_val=d_sample*((double)((SINE_EIGHTH)-index));
			fade_val/=(double)(SINE_EIGHTH);
			
			icing[tone]->eighth[index]=(short)fade_val;
		}
		angle+=step;
		if(angle>2*M_PI)angle-=2*M_PI;
	}	
			
}
void set_note_entry(tone)
int	tone;
{
double	base_fundamental;
double	base_octave;
double	base_third;
double	base_fourth;
double	base_fifth;


double	bend_cap;


double	bend_spread[BEND_MAX];

double	bend_step;

int	c;

	base_fundamental=musical_tones[tone];
	base_octave=base_fundamental*2;
	base_third=base_octave*1.05946309436*1.05946309436*1.05946309436
				*1.05946309436;
				
	base_fourth=base_third*1.05946309436;
	base_fifth=base_fourth*1.05946309436*1.05946309436;

	bend_cap=(0.05946309436/2)+1;
	bend_step=(bend_cap-1)/(BEND_MAX-1);
	
	bend_spread[0]=1;
	
	for(c=1;c<BEND_MAX;c++) bend_spread[c]=bend_spread[c-1]+bend_step;
	
	for(c=0;c<BEND_MAX;c++) {

// quarter	
		freq[tone]->quarter[c].fundamental=
		malloc(SINE_QUARTER*sizeof(short));
		collect_waves(base_fundamental,base_fundamental*bend_spread[c],
		freq[tone]->quarter[c].fundamental,SINE_QUARTER,
		WAVE_FUNDAMENTAL);
		
		freq[tone]->quarter[c].octave=
		malloc(SINE_QUARTER*sizeof(short));
		collect_waves(base_octave,base_octave*bend_spread[c],
		freq[tone]->quarter[c].octave,SINE_QUARTER,
		WAVE_OCTAVE);
		
		freq[tone]->quarter[c].third=
		malloc(SINE_QUARTER*sizeof(short));
		collect_waves(base_third,base_third*bend_spread[c],
		freq[tone]->quarter[c].third,SINE_QUARTER,
		WAVE_THIRD);
		
		freq[tone]->quarter[c].fourth=
		malloc(SINE_QUARTER*sizeof(short));
		collect_waves(base_fourth,base_fourth*bend_spread[c],
		freq[tone]->quarter[c].fourth,SINE_QUARTER,
		WAVE_FOURTH);
		
		freq[tone]->quarter[c].fifth=
		malloc(SINE_QUARTER*sizeof(short));
		collect_waves(base_fifth,base_fifth*bend_spread[c],
		freq[tone]->quarter[c].fifth,SINE_QUARTER,
		WAVE_FIFTH);
		
		
// eighth
		freq[tone]->eighth[c].fundamental=
		malloc(SINE_EIGHTH*sizeof(short));
		collect_waves(base_fundamental,base_fundamental*bend_spread[c],
		freq[tone]->eighth[c].fundamental,SINE_EIGHTH,
		WAVE_FUNDAMENTAL);
		
		freq[tone]->eighth[c].octave=
		malloc(SINE_EIGHTH*sizeof(short));
		collect_waves(base_octave,base_octave*bend_spread[c],
		freq[tone]->eighth[c].octave,SINE_EIGHTH,
		WAVE_OCTAVE);
		
		freq[tone]->eighth[c].third=
		malloc(SINE_EIGHTH*sizeof(short));
		collect_waves(base_third,base_third*bend_spread[c],
		freq[tone]->eighth[c].third,SINE_EIGHTH,
		WAVE_THIRD);
		
		freq[tone]->eighth[c].fourth=
		malloc(SINE_EIGHTH*sizeof(short));
		collect_waves(base_fourth,base_fourth*bend_spread[c],
		freq[tone]->eighth[c].fourth,SINE_EIGHTH,
		WAVE_FOURTH);
		
		freq[tone]->eighth[c].fifth=
		malloc(SINE_EIGHTH*sizeof(short));
		collect_waves(base_fifth,base_fifth*bend_spread[c],
		freq[tone]->eighth[c].fifth,SINE_EIGHTH,
		WAVE_FIFTH);
		
		
// sixteenth SIXTEENTH
		freq[tone]->sixteenth[c].fundamental=
		malloc(SINE_SIXTEENTH*sizeof(short));
		collect_waves(base_fundamental,base_fundamental*bend_spread[c],
		freq[tone]->sixteenth[c].fundamental,SINE_SIXTEENTH,
		WAVE_FUNDAMENTAL);
		
		freq[tone]->sixteenth[c].octave=
		malloc(SINE_SIXTEENTH*sizeof(short));
		collect_waves(base_octave,base_octave*bend_spread[c],
		freq[tone]->sixteenth[c].octave,SINE_SIXTEENTH,
		WAVE_OCTAVE);
		
		freq[tone]->sixteenth[c].third=
		malloc(SINE_SIXTEENTH*sizeof(short));
		collect_waves(base_third,base_third*bend_spread[c],
		freq[tone]->sixteenth[c].third,SINE_SIXTEENTH,
		WAVE_THIRD);
		
		freq[tone]->sixteenth[c].fourth=
		malloc(SINE_SIXTEENTH*sizeof(short));
		collect_waves(base_fourth,base_fourth*bend_spread[c],
		freq[tone]->sixteenth[c].fourth,SINE_SIXTEENTH,WAVE_FOURTH);
		
		freq[tone]->sixteenth[c].fifth=
		malloc(SINE_SIXTEENTH*sizeof(short));
		collect_waves(base_fifth,base_fifth*bend_spread[c],
		freq[tone]->sixteenth[c].fifth,SINE_SIXTEENTH,WAVE_FIFTH);
	}		
}


void make_waves(freq1,freq2,sine_buf,dur,type)
double	freq1;
double	freq2;
short	*sine_buf;
int	dur;
char	type;
{


double per1;
double per2;

double per_init;
double step_init;

double	step;
double	step2;
double	step_step;
double	angle;
double	d_sample;
int	index;
double	my_sin;	// double nothing is still nothing :(
bool	is_bend;
double	sustain;
double	sustain_step;

double	init_freq;
	
	switch(type) {
	
		case WAVE_FUNDAMENTAL:	sustain_step=1/SUSTAIN_FUNDAMENTAL;
					break;
					
		case WAVE_OCTAVE:	sustain_step=1/SUSTAIN_OCTAVE;
					break;
					
		case WAVE_THIRD:	sustain_step=1/SUSTAIN_THIRD;
					break;
					
		case WAVE_FOURTH:	sustain_step=1/SUSTAIN_FOURTH;
					break;
					
		case WAVE_FIFTH:	sustain_step=1/SUSTAIN_FIFTH;
	}
	
	init_freq=freq1+(((freq1*1.05946309436)-freq1)/4);

	per_init=rate/init_freq;
	step_init=(2*M_PI)/per_init;
		
	sustain=1;
	
	is_bend=(freq1!=freq2);
	
	per1=rate/freq1;
	step=(2*M_PI)/per1;	


if(is_bend) {
	per2=rate/freq2;
	step2=(2*M_PI)/per2;
}
	
	step_step=(step-step_init)/SHARP_ATTACK;
	
	angle=2*M_PI;
		
	for(index=0;index<(dur);index++) {
		my_sin=sin(angle);


		d_sample=my_sin*BASS_REF_VAL;
		d_sample*=(type==WAVE_FUNDAMENTAL)?BASS_CLIPVAL:T_CLIPVAL;
		
		d_sample*=sustain;
		sustain-=sustain_step;
		
		if(d_sample>BASS_PEAK) d_sample=BASS_PEAK;
		if(d_sample<BASS_PEAK*-1) d_sample=BASS_PEAK*-1;
		
		
if(type!=WAVE_FUNDAMENTAL) {
	d_sample*=TRANSVERSE_LEVEL;
	d_sample/=32767;
}

		sine_buf[index]=(short)d_sample;
		
		if(index<SHARP_ATTACK) {
			angle-=step;
			if(angle<0)angle+=(2*M_PI);
			step+=step_step;
			if(index==(SHARP_ATTACK-1))
				step_step=(step-step2)/
				(double)(dur-SHARP_ATTACK);
		} else {
			angle+=step;
			if(is_bend)step+=step_step;
			if(angle>(2*M_PI)) angle-=(2*M_PI);
		}
	}
}

void collect_waves(freq1,freq2,sine_buf,dur,type)
double	freq1;
double	freq2;
short	*sine_buf;
int	dur;
char	type;
{
int		c;
int		sample_int;

	make_waves(freq1,freq2,sine_buf,dur,type);
	
	make_waves(freq1,freq2,collecter,dur*2,type);

	for(c=0;c<dur;c++) {
		sample_int=sine_buf[c]+((collecter[c*2]*TRANSVERSE_LEVEL)/32767);
		if(sample_int>32767)sample_int=32767;
		if(sample_int<-32768)sample_int=-32768;
		sine_buf[c]=sample_int;
	}
	
	make_waves(freq1,freq2,collecter,dur*4,type);

	for(c=0;c<dur;c++) {
		sample_int=sine_buf[c]+((collecter[c*4]*TRANSVERSE_LEVEL)/32767);
		if(sample_int>32767)sample_int=32767;
		if(sample_int<-32768)sample_int=-32768;
		sine_buf[c]=sample_int;
	}	
	make_waves(freq1,freq2,collecter,dur*8,type);

	for(c=0;c<dur;c++) {
		sample_int=sine_buf[c]+((collecter[c*8]*TRANSVERSE_LEVEL)/32767);
		if(sample_int>32767)sample_int=32767;
		if(sample_int<-32768)sample_int=-32768;
		sine_buf[c]=sample_int;
	}	

}



void pivotSort(myList,count,mySwap)
void **myList;
int count;
unsigned char mySwap();
{
int c;

int l_index, h_index;
int l_count, h_count;

void **proc;
void *pp;

int pivot;
void **pivotList;
int pivotIndex;
int pivotChunk;
int pivotMax;

unsigned char result;

	// This function is recursive so the beginning is a good place to check for exit conditions
	
	// There is no sorting fewer than two items
	if(count<2) return;
		
	// If there are exactly two, set them correctly and return.  The algorithm 
	// would work but why run it again if not needed? 

	if(count==2) {
	
		result=mySwap(myList[0],myList[1]);
		result=(result==SORT_PIVOT_FIRST_SWAP)?SORT_SWAP:result;
		
		if(result==SORT_SWAP) {
			pp=myList[0];
			myList[0]=myList[1];
			myList[1]=pp;				
		} 
		return;
	}
	
	// Let's find the pivot
	pivot=-1;

	for(c=1;c<count;c++) {
		result=mySwap(myList[c-1],myList[c]);
		
		switch(result) {
					case SORT_PIVOT_FIRST_SWAP:
					case SORT_PIVOT_FIRST_KEEP:
					case SORT_PIVOT_EQUAL:
		pivot=c-1;
		break;;
		
					case SORT_PIVOT_SECOND_SWAP:
					case SORT_PIVOT_SECOND_KEEP:
		pivot=c;
		break;;
				
		}
		if(pivot!=-1)break;
	}

	// There wasn't one, so nothing is out of order.  Go back
	if(pivot==-1) return;
	
	// Next, size of memory allocation for the pivot list.  Below a certain size for count, just use that
	if(count<PIVOT_SORT_MIN) { // note that in this case, pivotMax is not supposed to be reached.
		pivotChunk=count; 
		pivotMax=PIVOT_SORT_MIN; 
	// ..otherwise, assume one quarter the size and add more as needed later.
	// Be just over a quarter rather than just under if we can't be exact, although the other would work.  			
	// It's just that the last reallocation would barely be used.
	} else {
		pivotChunk=count/4;
		if((count%4)>0)++pivotChunk;
		pivotMax=pivotChunk;
	}

	// get the working buffer...
	proc=malloc(count*sizeof(void *));

	// get this to store all the pivot-equal pointers in, including the actual pivot
	pivotList=malloc(pivotChunk*sizeof(void *));


	// initialize...		
	l_index=0;
	h_index=count-1;

	pivotIndex=0;
	l_count=0;
	h_count=0;
	
	// The meat of the matter.  Place <pivot from zero up, >pivot from top down, and make ==pivot list
	for(c=0;c<count;c++) {

		result=(c==pivot)?SORT_EQUAL:mySwap(myList[c],myList[pivot]);
		
		switch(result) {
							case SORT_EQUAL:
							case SORT_PIVOT_EQUAL:
		pivotList[pivotIndex]=myList[c];
		pivotIndex++;
		if(pivotIndex==pivotMax) {
			pivotMax+=pivotChunk;
			pivotList=realloc(pivotList,pivotMax*sizeof(void *));
			if(pivotList==NULL) {
				printf("sort: wtf: realloc\n");
				exit(EXIT_FAILURE);
			}
		}
		continue;;				
							case SORT_KEEP:	
							case SORT_PIVOT_FIRST_KEEP:
							case SORT_PIVOT_SECOND_KEEP:
		proc[l_index]=myList[c];
		++l_index;
		++l_count;	
		continue;;
		//					case SORT_PIVOT_FIRST_SWAP:
		//					case SORT_PIVOT_SECOND_SWAP:
		} 	// if it wasn't any of the previous, it has to go to the post segment					
			// Here will also go any unusual returns from the sorter, so that's
			// defined behavior now.

	// use of SORT_PIVOT_*_SWAP constants encouraged!  But will end up here anyway.
	// No need to throw an error at unexpected returns, we're a sort function, not the logic police	
		proc[h_index]=myList[c];
		--h_index;  // <- needs incrementing at end of loop 
		++h_count;
	}

	// Put the pivots where they go.  This is naturally pointed to by l_index
	// Note that this is where they ULTIMATELY go.  This is not temporary, these pointers are
	// not included in the subsorts below.
	memcpy(&(proc[l_index]),pivotList,pivotIndex*sizeof(void *));

	// Get rid of the temporary heap memory	
	free(pivotList);
	memcpy(myList,proc,sizeof(void *)*count); // Grabbing the results of our work of course
	free(proc);
	
	// This needs adjusting as noted above, not like l_index, which we are now done with 
	if(h_count>0)++h_index;
	
	// ...aaaaaand we're done.  Invoke child instances and default to return
	if(l_count>1)pivotSort(myList,l_count,mySwap);
	if(h_count>1)pivotSort(&(myList[h_index]),h_count,mySwap);	
}	

