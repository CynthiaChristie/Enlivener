/* ==========================================================================
 * Enlivener - A Time-Domain Live Guitar Tracker for Linux
 * File: enlivener.c
 * Description: Listens to incoming guitar audio, tracks fundamentals in the 
 *              time domain, and generates live, consonant backing music.
 * Author: Cynthia Christie - https://www.youtube.com/@enlivener-d7f
 * License: Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)
 * ========================================================================== */

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <libgen.h>
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
#include <sys/stat.h>
#include <signal.h>
#include <ctype.h>
#include <termios.h>
#include <sched.h>

#include "pivotsort.h"
#include "enlivener.h"


// Function prototype declarations.  Function uses described succinctly
// here.  Longer explanations where needed found above the actual code
// for them.

// A fairly generic pivot sort and its helper function
void		pivotSort();
unsigned char	sort_phase();


// Generates the low amplitude sine wave examples for signal comparison.
FREQ_FRONT	*generate_sine_data();

// signal analysis functions
void		get_cycle();		// overall controller of that process
void		get_phase_variance();	// loop controller of note/phase combos
void		cancel_check();		// invoked by above for actual work

// synth related functions
char		*get_synth_exe();	// Fine the synth executable
void		set_synth_state();	// For signal communication with synth


void		exit_cleanly();	// Print a nice goodbye to the user, set
				// the terminal attributes the way we found
				// them, and exit.  SIGINT is trapped to
				// divert to this.
				


void 		write_beats_init();	// Get the drum samples from the wave file
void 		write_beat_dat();	// Translate that into an internal format
					// for the synth
					
int		get_file_bpm();		// try to get a bpm out of the drum file
					// name
					
short		*retime();	// Wave stretcher for if the drum file doesn't
				// do the bpm boundary cleanly.

char		*tone_set_str();	// Formatter of the list of tones
					// the synth reports that it understands
					// to be the usable set.



// the everything text buffer.  Called playcmd for historical reasons that are
// humourous to me.  It really was for a shell command in a completely different
// app, and the name copied over with some network communication code that
// I've since discarded in favour of shared memory.
char		playcmd[MISC_TEXT];


int		rate;		// Dictated by drum file rate, if exists

int		max_tone;	// Number of musical notes



FREQ_FRONT	**freq;		// Objects for each note holding a set of phases
				// as below.
				
PHASE		**phase;	// A set of note/phase combinations

bool		synth_ready;	// signal communications control flag

bool 		have_drums;	// Was a valid drums file according to
				// command line specificatoin?

KEY_INFO	*key_info;	// Shared memory block for reading synth
				// internal state

int		bpm;		// What it says on the box
int		a_bpm;		// bpm as given on command line
int		f_bpm;		// bpm as read from file name


struct termios new_attr;	// So we can override usual term settings
struct termios old_attr;	// for keyboard volume control


int		tones[3];	// Tones recognized by get_cycle()


FRONT_INFO	*front_info;	// Memory block for sharing with synth


// Labels for reporting in status line
char *note[] = { "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B", "C",
		"C#/Db", "D", "D#/Eb" };


// The actual frequencies that the app tries to match against the signal
double musical_tones[]={
/*
20.60172, 21.82676, 23.12465, 24.49971, 25.95654, 27.50000,
29.13524, 30.86771, 32.70320, 34.64783, 36.70810, 38.89087,
*/

// 41.20344, 43.65353, 46.24930, 48.99943, 51.91309, 55.00000,
// 58.27047, 61.73541, 65.40639, 69.29566, 73.41619, 77.78175,

82.40689, 87.30706, 92.49861, 97.99886, 103.8262, 110.0000, // E-A
116.5409, 123.4708, 130.8128, 138.5913, 146.8324, 155.5635, // Bb-Eb

164.8138, 174.6141, 184.9972, 195.9977, 207.6523, 220.0000, 
233.0819, 246.9417, 261.6256, 277.1826, 293.6648, 311.1270, 

329.6276, 349.2282, 369.9944, 391.9954, 415.3047, 440.0000, 
466.1638, 493.8833, 523.2511, 554.3653, 587.3295, 622.2540, 

659.2551, 698.4565, 739.9888, 783.9909, 830.6094, 880.0000, 
932.3275, 987.7666, 1046.502, 1108.731, 1174.659, 1244.508, 

// 1318.510, 1396.913, 1479.978, 1567.982, 1661.219, 1760.000, 
// 1864.655, 1975.533, 2093.005, 2217.461, 2349.318, 2489.016, 

// 2637.020, 2793.826, 2959.955, 3135.963, 3322.438, 3520.000, 
// 3729.310, 3951.066, 4186.009, 4434.922, 4698.636, 4978.032,
/*
5274.041, 5587.652, 5919.911, 6271.927, 6644.875, 7040.000,
7458.620, 7902.133, 8372.018, 8869.844, 9397.273, 9956.063,
*/
 0 };




int main(argc,argv)
int argc;
char *argv[];
{

// Required objects for audio handler
pa_simple	*pa_index_in;
pa_sample_spec	ss_in;
int	pa_says,pa_err;

// Stucture for system call to tell us how many columns in the terminal
struct winsize w;

// This is because we make lots of use of clock_gettime
struct timespec tp;


// misc counters
int	c;
int	d;

// A tracking of how long the player has been silent, to determine when to 
// trigger a fade out
int	dead_buffers;

// Buffer that holds incoming guitar signal
short	*audio;

// index for building the array of note/phase combinations
int	phase_index;

// pointer to a string containing the pathname of the synth
char		*synth_exe;

// The synth's process id
pid_t		synthpid;

// A file descriptor associated with the drum track
int		dat_fd;

// Is this either the actual initialzation or possibly a post silence period?
bool		first_run;


// Related to the timer and planned finishes
long		start_time;
long		elapsed;

long		end_minutes;
long		end_seconds;
long		actual_end;

// shared memory file descriptor
int		shm_fd;

// Pointer to the argv element that holds the drum pathname
char		*drum_arg;

// Indicates if a bass volume was found on the command line
bool		found_bass_vol;

// Part of easy going argv interpreter
int		arg_limit;

// a frequently updated object to hold the number of terminal columns
int	columns;

// As we transition to the code, the following two items, the declaration and 
// the sigaction(), are just so this app doesn't risk leaving behind zombie
// tasks.

struct sigaction sa;

	sigaction(SIGCHLD,&sa,NULL);

// Get terminal attributs into one object and copy them to a backup object
	tcgetattr(STDIN_FILENO, &new_attr);
	memcpy(&old_attr,&new_attr,sizeof(new_attr));
        
// Initialize the all purpose text buffer
	bzero(playcmd,MISC_TEXT);
	
// Put the shared memory front-share name in it.
	sprintf(playcmd,SHM_FRONT_FMT,geteuid());
	
// create it and error check, doing the mapping to front_info if successful
	shm_fd=shm_open(playcmd,O_RDWR|O_CREAT|O_TRUNC,
	S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	
	if(shm_fd==-1) {
		fprintf(stderr,"front: Can't open shared memory\n");
		exit(EXIT_FAILURE);
		// no point in running in this case.  No sound anyway
	} else {
		ftruncate(shm_fd,sizeof(FRONT_INFO));
		front_info=mmap(NULL,sizeof(FRONT_INFO),PROT_READ|PROT_WRITE,MAP_SHARED,
			shm_fd,0);
		if(front_info==MAP_FAILED) {
			fprintf(stderr,"front: mmap failed\n");
			exit(EXIT_FAILURE);
			// As above
		}
		
	}

	close(shm_fd);	
		
// Initialize the memory this app shares with the synth	
	front_info->tones[0]=-1;
	front_info->tones[1]=-1;
	front_info->tones[2]=-1;
	front_info->synth_proceed=false;
	front_info->reached_end=false;
	
	clock_gettime(CLOCK_MONOTONIC,&tp);
	
	front_info->note_history_index=0;

// This is the initialization of a "harmony stack" that is shared with the synth
// We give all detected notes a place in this stack and a time stamp by which
// the synth will decide what to do with it.  The purpose of it is so that the
// synth can build a set of tones that's consonant with what's happening in
// the moment.
//
// They entries are all stamped with the current time to guarantee they
// will be found to be older by the main loop, a necessary part of the 
// functioning.

	for(c=0;c<NOTE_HISTORY_LIMIT;c++) {
		front_info->note_history[c].tone=-1;
		front_info->note_history[c].played_at=NANONANO;
	}

	front_info->bass_vol=FRONT_BASS_VOLUME;
	front_info->drum_vol=FRONT_DRUM_VOLUME;

// misc other initializing
	rate=48000;
	synth_ready=false;
	bpm=150;
		
	signal(SIGINT,exit_cleanly);
	signal(SIGUSR1,set_synth_state);

	first_run=true;

	dead_buffers=0;
			
	have_drums=false;

// bpm detector flags.  a_bpm means argv bpm, f_bpm means filename-embedded bpm

	f_bpm=-1;
	a_bpm=-1;
	
	dat_fd=-1;
	
	drum_arg=NULL;
	found_bass_vol=false;
	arg_limit=5;
	a_bpm=-1;
	actual_end=-1;
				
// A loop to try to parse the command line arguments in a not too rigid way.
	if(argc>1) for(c=1;c<argc;c++) {
		printf("%s...let's see what that is: ",argv[c]);
		d=sscanf(argv[c],"%ld:%2ld",&end_minutes,&end_seconds);
		if(d==2) {
			printf("It's an end marker.\n");
			actual_end=(60*end_minutes)+end_seconds;
			if(c<arg_limit) continue;
			else break;
		}
		// d=atoi(argv[c]);
		for(d=0;d<strlen(argv[c]);d++) if(!isdigit(argv[c][d])) break;
		
		if(d==strlen(argv[c])) {
		d=atoi(argv[c]);
		if(
		(strlen(argv[c])<4) &&
		(d>=0)
		&&
		(d<=100)) {
			printf("It's volume - assigning to ");
			if(!found_bass_vol) {
				printf("bass synth.\n");
				found_bass_vol=true;
				front_info->bass_vol=(d/5)*5;
			} else {
				printf("drums.\n");
				front_info->drum_vol=(d/5)*5;
			}
			if(c<arg_limit)continue;
			else break;
		}
		}
		if(argv[c][0]=='b'&&((strlen(argv[c])-1)<4)) {
			d=atoi(&(argv[c][1]));
			if((d>=BPM_MIN)&&(d<=BPM_MAX)) {
				printf("It's a bpm.\n");
				a_bpm=d;
				if(c<arg_limit)continue;
				else break;		
			}
		}
		
		if(drum_arg==NULL) {
			dat_fd=open(argv[c],O_RDONLY);
			if(dat_fd!=-1) {
				printf("It's a readable file, so I'm taking it for a drum track.\n");
				drum_arg=argv[c];
				if(c<arg_limit)continue;
				else break;
			}
		}
		printf("It's not a volume or a readable file or otherwise useful.\n");
		++arg_limit;			
	}	

// If a drum track was found, see if a bpm is embedded in the file name
	if(dat_fd!=-1) {
		f_bpm=get_file_bpm(drum_arg);
		close(dat_fd);
	}

// Use the above if bpm not otherwise specified on the command line
	if(a_bpm==-1)bpm=f_bpm;
	else bpm=a_bpm;
	
// If neither method above worked, the bpm will now be -1 and should be set to 
// default.

	if(bpm==-1)bpm=120;

// Finally, share that info with the synth	
	front_info->bpm=bpm;

// If we found a drum track, load it and get it ready for the synth

	if(drum_arg!=NULL)write_beats_init(drum_arg);

// Initialize audio connection	
ss_in.format = PA_SAMPLE_S16NE;
ss_in.channels = 1;
ss_in.rate = rate;

pa_index_in = pa_simple_new(NULL,               // Use the default server.
                  "Enlivener Analysis",	      // Our application's name.
                  PA_STREAM_RECORD,
                  NULL,               // Use the default device.
                  "Auto Guitar Backing",   // Description of our stream.
                  &ss_in,                // Our sample format.
                  NULL,               // Use default channel map
                  NULL,               // Use default buffering attributes.
                  &pa_err               // Ignore error code.
                  );

// Count the musical tones in the array of global floats declared above
	for(max_tone=0;musical_tones[max_tone]!=0;max_tone++);

// freq - hold the data for sine data and phase points to be cross referenced by...
	freq=malloc(max_tone*sizeof(FREQ_FRONT *));
	
// phase - a list of pointers to reference the above data in a sortable way
// where the phases are independent of the note group they were created with.
	phase=malloc(max_tone*MAX_PHASE*sizeof(PHASE *));
	
// initialize the phase count
	phase_index=0;

	for(c=0;c<max_tone;c++) {
	
		// grab an appropriately sized buffer
		freq[c]=malloc(sizeof(FREQ_FRONT));
		
// the function in the second argument below, generate_sine_data, returns a 
// pointer to a static FREQ_FRONT object that it fills with information for
// us, but that we have to copy out right away.
//
memcpy(freq[c],generate_sine_data(musical_tones[c],AUDIO_READ),sizeof(FREQ_FRONT));

// Here, the phase array is not malloced as with freq, but rather populated
// with pointers drawn from that array.
		for(d=0;d<MAX_PHASE;d++) {
			phase[phase_index]=&(freq[c]->phase[d]);

// Throughout this code, "note" means a number from 0-11 mapping to E-D#,
// but tone is a specific frequency referenced in the musical_tones array
			phase[phase_index]->note=c%12;
			phase[phase_index]->tone=c;

			phase_index++;
		}

	}

// If the SYNTH_EXECUTABLE string constant has no '/' characters call 
// get_synth_exe() which will look for it everywhere in the environment 
// variable $PATH
if(!strcmp(SYNTH_EXECUTABLE,basename(SYNTH_EXECUTABLE)))synth_exe=get_synth_exe();
else synth_exe=SYNTH_EXECUTABLE;
	
	// No point running if no synth
	if(synth_exe==NULL) {
		printf("enlivener: Unable to start synth - exiting\n");
		exit(EXIT_FAILURE);
	}

	// Now spawn it
	synthpid=fork();
	
	playcmd[3]='\0';
	sprintf(playcmd,"%03d",bpm);
	
	// If those code block runs, we were supposed be the synth but
	// the code didn't load.  Exit.
	if(synthpid==0) {
		execl(synth_exe,basename(synth_exe),playcmd,(char *)NULL);
		printf("Created process for synth, but synth did not start\n");
		kill(getppid(),SIGKILL);
		exit(EXIT_FAILURE);
	}
	
	// The above will look like this to the original process, which can
	// also therefore exit.
	if(synthpid==-1) {
		printf("enlivener: failed to spawn synth\n");
		exit(EXIT_FAILURE);
	}

	// We're presuming we launched our synth now and not the robot
	// apolcalypse.
	printf("Using synth: %s\n",synth_exe);

	// Get audio input buffer
	audio=malloc(AUDIO_READ*sizeof(short));

	if(synth_ready==false) while(!synth_ready);
	// It could easily have finished initializing faster and already 
	// signalled us	

	synth_ready=false;
	// And that's how we'll do it!  Always set it to false again right
	// away!

	// Prepare text buffer to handle share name of synth shared memory
	bzero(playcmd,MISC_TEXT);
	
	// Set the name
	sprintf(playcmd,SHM_SYNTH_FMT,geteuid());
	
	// Open the synth's shared memory read only
	shm_fd=shm_open(playcmd,O_RDONLY,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	
	if(shm_fd==-1) fprintf(stderr,"front: failed to open shm fd\n");
	else {
		key_info=mmap(NULL,sizeof(KEY_INFO),PROT_READ,MAP_SHARED,shm_fd,0);
		if(key_info==MAP_FAILED) fprintf(stderr,"front: cant mmap\n");
	}

	close(shm_fd);
	
	// Flip the terminal icanon and echo flags for keyboard input
	new_attr.c_lflag &= ~(ICANON | ECHO);
	new_attr.c_cc[VMIN]=0;
	new_attr.c_cc[VTIME]=0;
	tcsetattr(STDIN_FILENO, TCSANOW, &new_attr);




putchar('\n');

printf("Enlivener: Live guitar backing generator for Linux (Also my artist name)\n");
printf("(C) Cynthia Christie 2026\n");
printf("under Creative Commons Attribution-NonCommercial 4.0 International Public License\n");
putchar('\n');
printf("%d bpm\n\n",bpm);

clock_gettime(CLOCK_MONOTONIC,&tp);

start_time=NANONANO;

//////////////////////
for(;;) {


// local object to handle user input
char	user_keys[3];

	// Find the number of columns so the \r character works correctly.
	// It's in the loop because the user might resize it.
	ioctl(STDIN_FILENO, TIOCGWINSZ, &w);
	columns=w.ws_col?(w.ws_col-1):75;

	// Before reading the audio input, flush any historical data
	// and get the buffer from the present moment.
	
	pa_simple_flush(pa_index_in,&pa_err);	        			
	pa_simple_read(pa_index_in,audio,AUDIO_READ*sizeof(short),&pa_err);

	// Find out what note(s) are in it		
	get_cycle(audio,AUDIO_READ);


// The following is all so the synth can figure out what else it's ok to play
// besides the notes it's directly told

	// Get a fine time stamp
	clock_gettime(CLOCK_MONOTONIC,&tp);
	
	// Increment the harmony stack index and wrap it if appropriate
	(front_info->note_history_index)++;
	if(front_info->note_history_index==NOTE_HISTORY_LIMIT)
		front_info->note_history_index=0;
		
	// Place the first note in the tones stack, which will usually be
	// the only one, on the next stack spot and time stamp it.
	front_info->note_history[front_info->note_history_index].tone=tones[0];
	front_info->note_history[front_info->
	note_history_index].played_at=NANONANO;

	// initialize the text buffer for the output line	
	bzero(playcmd,columns+1);


	// If an end was marked, check if we've reached it
	elapsed=(NANONANO-start_time)/1000000000;
	
	if(actual_end!=-1) if(elapsed>actual_end) {
		front_info->reached_end=true;
		printf("\n\nReached end marker!\n\n");
		actual_end=-1;
	}
	
	// See if get_cycle() reported silence or if we are either initializing
	// or coming back from quiet.
	
	if(tones[0]==-1) {
		if(first_run) {
			// If first or in quiet mode
			start_time=NANONANO;
			sprintf(playcmd,"Ok, start playing.");
		} else sprintf(playcmd,"%ld:%02ld - I don't hear anything",
			elapsed/60,elapsed%60);
			
		
	} else {

	// One time switch to prevent the synth initializing faster than the
	// front, which it will, and having undefined behavior		
	front_info->synth_proceed=true;
	
	// Start the display timer
	if(first_run)start_time=NANONANO;
	
	// Set to true again if we enter quiet mode.
	first_run=false;

	// Start status line with timer
	sprintf(playcmd,"%ld:%02ld - ",elapsed/60,elapsed%60);
	
	// Print the volumes
	sprintf(&(playcmd[strlen(playcmd)]),"b%3d d%3d",
	front_info->bass_vol,front_info->drum_vol);
	
	// Report what get_cycle() found
	sprintf(&(playcmd[strlen(playcmd)])," - Heard: %s  (",
	tone_set_str(tones));
	
// Process feedback info from synth

	// which may have an idea what the key is, or report -1 if not
	if(key_info->key_is==-1) {

	char fin;

	// The famous shrug emoji if the synth can't figure out any notes
	// other than the ones directly reported to it.
	char shrug[]={
	194,175,92,95,40,227,131,132,41,95,47,194,175,0 };
	
	
		if(key_info->accents_count==0) {
			strcpy(&(playcmd[strlen(playcmd)]),shrug);
		}
		
		// If it does know some notes but not a whole key,
		// print those in brackets
		for(c=0;c<key_info->accents_count;c++) {
			fin=(c==key_info->accents_count-1)?'\0':' ';
			if(strlen(note[key_info->accents[c]])==1)
			sprintf(&(playcmd[strlen(playcmd)]),"%c%c%c",
			note[key_info->accents[c]][0],fin,fin);
			
			else
			
			sprintf(&(playcmd[strlen(playcmd)]),"%c%c%c",
			note[key_info->accents[c]][0],
			note[key_info->accents[c]][1],fin);
		}
		playcmd[strlen(playcmd)]=')';
		
		
	} else
	// Print a key since we think we know it
	sprintf(&(playcmd[strlen(playcmd)]),"%s Minor) ",
		note[key_info->key_is]);
		
	// print a * if we've picked out a triad - a very rare thing
	sprintf(&(playcmd[strlen(playcmd)])," - %s %c",
	tone_set_str(key_info->tones),(key_info->triad)?'*':' ');
	
	}
	
	// Truncate the output string to the column length
	playcmd[columns+1]='\0';
	while(strlen(playcmd)<columns) playcmd[strlen(playcmd)]=' ';
	
	// And print it with a rewind character.
	printf("%s\r",playcmd);
	fflush(stdout);

	
	// If get_cycle() reported silence, add that to dead_buffers
	if(tones[0]==-1)++dead_buffers;
	
	// After threshold MAX_NO_SOUND, make it like we just started.
	if(dead_buffers==MAX_NO_SOUND) {
		dead_buffers=0;
		first_run=true;
		start_time=NANONANO;
	} 
	
	
	// Get and apply keyboard input about volume changes.
	
	// For sudden release version Oct 6 2026
	// commented out and not documented because crashy.
	
/*
	bzero(user_keys,3*sizeof(char));
	
	c=read(STDIN_FILENO,user_keys,3);
	
	
	if((user_keys[0]!=27)||(user_keys[1]!=91)) continue;
	
	if((user_keys[2]==65)&&(front_info->bass_vol<100))
	(front_info->bass_vol)+=5;
	
	if((user_keys[2]==66)&&(front_info->bass_vol>0))
	(front_info->bass_vol)-=5;
	
	if((user_keys[2]==67)&&(front_info->drum_vol<100))
	(front_info->drum_vol)+=5;
	
	if((user_keys[2]==68)&&(front_info->drum_vol>0))
	(front_info->drum_vol)-=5;

*/
//	clock_gettime(CLOCK_MONOTONIC,&tp);
	

}	

///////////////////////	

}

// get_cycle: This is the function that actually picks the notes out of
// the audio buffers it gets from the guitar.  It uses a homebrew method
// that I built, not Fourier.  The short explanation as to why that is, is
// that I experienced a lot of brainhurt and frustration trying to understand
// how to implement the common Fourier libraries and adapt them to my purposes.

// Making my own thing turned out to be a lot easier.

// How it works:  It has in memory a reference sine wave for each musical note,
// and for each of those, MAX_PHASE number of offsets.  For each of those offsets,
// it brute force tests how well that wave, starting from that offset, cancels
// the audio from the guitar buffer.  Now we can't just take the number one 
// winner because that might be wrong.  What we then do is:
//
//	-sort the list of phases (all notes) by how well each did cancellation

//	-check to see which note or notes best fit the expected transverse
//	wave profile and return the best fits.

//	If none fit very well, just check which note or notes were counted the
//	most times as having provided some cancellation benefit.

//	Interpret a lack of any cancellation benefit at all as silence and return.

void get_cycle(audio,len)
short *audio;
int len;
{
int	c, d;		// misc counters, of course

int	phase_index;	// Initialized as the number of musical notes times
			// the number of phases processed for each, changed
			// mid-function to a smaller number which is why
			// there's an object for it;
			
int 	note_tally[12];

//int		tone;
//int		period;
//static	int	tones[3];

int		tone_index;	// How many equally likely candidates there
				// are for being the correct note to a
				// maximum of three, with actual numbers
				// normally being zero or one.
				
// Note: for the following, as in many places, I write "12" rather than 
// a symbolic constant because the number IS 12.  The app is fundamentally
// based on a 12-tone equal temperment music scale.  The number is never
// not going to be 12.

NOTE_CHECK	note_check[12];	// A structure to hold counters for the
				// presence of the note and it's transverse
				// elements in the input buffer

int	note_candidate[12];	// An array to indicate how each note is faring
				// as a candidate
				
int	note_candidate_count;	// Which, not every note is.


// Initialize the array we will be placing found notes in.
	tones[0]=-1;
	tones[1]=-1;
	tones[2]=-1;
	tone_index=0;
	
// Begin by considering all note and phase combinations.  Not that since
// all such need to be considered every time, the previous instance of this
// function did not restore the original order of the "phase" array because
// there is no need that this be done.
	
	phase_index=MAX_PHASE*max_tone;
	
	get_phase_variance(phase,phase_index,audio,len);
	
	
// Sort the phase array by the greatest values assigned to phase->benefit
// in cancel_check(), which function is called by get_phase_variance above.
	pivotSort(phase,phase_index,sort_phase);

// If the result of that is that phase[0]->benefit is zero, that means there
// is no recognizable note in the buffer and it is likely silent.  Write the
// indicator of this to the shared memory and return.
	if(phase[0]->benefit==0) {
		front_info->tones[0]=-1;
		front_info->tones[1]=-1;
		front_info->tones[2]=-1;
		return;
	}

// Reduce phase_index to exclude all phases that have no signal cancellation
// benefit at all.
	for(phase_index=0;phase_index<(max_tone*MAX_PHASE);phase_index++)
	if(phase[phase_index]->benefit==0)break;

// And even of those, only consider the top PHASE_PERCENT of that.
	phase_index=(phase_index*PHASE_PERCENT)/100;
		
// Clear any garbage from this array
	bzero(note_check,12*sizeof(NOTE_CHECK));
	
// For every surviving phase element, add it's note (E-D#) to it's 
// place in the root counter of the note_check array.  Octaves not important.
	for(c=0;c<phase_index;c++) 
	note_check[phase[c]->note].roots++;

// Convenient temporary object since we have to jump predictable steps off
// the actual counter every time	
int this_note;

// This loop is to help identify the correct note based on the fact that a 
// guitar string always presents transverse elements as well as octave overtones
// of the actual note.  We here check for this, so the counter c iterates
// through the notes E-D#, while the temporary object this_note jumps to
// check for transverse elements of the note we're confirming.  Those are the
// major third, the fourth, and the fifth of the current note.

	for(c=0;c<12;c++) {
	
		// So we set the temporary object to the current note
		this_note=c;
		
		// If we don't even have roots for this note, continue
		if(!(note_check[this_note].roots)) continue;
		
		// Increment this_note to be the major third.
		this_note+=4;
		
		// Account for that crossing an octave boundary
		if(this_note>11)this_note-=12;
		
		// If the major third is indeed present, add that to the
		// transverse count.
		if(note_check[this_note].roots)++note_check[c].transverses;
		
		// Repeat for the fourth
		this_note++;
		if(this_note>11)this_note-=12;
		if(note_check[this_note].roots)++note_check[c].transverses;
		
		// Repeat for the fifth
		this_note+=2;
		if(this_note>11)this_note-=12;
		if(note_check[this_note].roots)++note_check[c].transverses;

	}
	

// Arbitrarily assign the first element of phase to be the currently winning
// candidate so the loop works.  It can be literally any of them.

	note_candidate_count=1;
	note_candidate[0]=phase[0]->note;

// So, once again iterating through E-D#...		
	for(c=0;c<12;c++) {

// If this is the current winner, continue.		
if(c==note_candidate[0])continue;

// If this has more transverse elements than the current winner(s), reset
// number of winners to 1, and make it this one.
if(note_check[c].transverses>note_check[note_candidate[0]].transverses) {
	note_candidate_count=1;
	note_candidate[0]=c;
	continue;
}

// If it's a tie with the current winner(s), register that.
if(note_check[c].transverses==note_check[note_candidate[0]].transverses) {
	note_candidate[note_candidate_count]=c;
	note_candidate_count++;
}
	
	}		

// If there's one clear winner, we're done.  Write that to the shared memory
// and go back.
	if(note_candidate_count==1) {
		front_info->tones[0]=note_candidate[0];
		front_info->tones[1]=-1;
		front_info->tones[2]=-1;
		return;
	}

// From here, the code only executes if the foregoing did not fine one, single
// clear winner.  An attempt to resolve a tie amongst winners of the transverse
// elements contest now takes place.
	
// Convenient temporary object.
int most_roots;

// tone_index is "number of notes that will be written to shared memory for the
// synth"
	
	tone_index=0;

// We start from the assumption that there will be one clear winner and prepare
// accordingly.  tones[0] need not be initialized because it WILL be assigned.	
	tones[1]=-1;
	tones[2]=-1;


for(tone_index=0;tone_index<3;tone_index++) {	

	// most_roots is what it says, what's the most roots we've found
	// among the candidates so far?  It is not an index.
	most_roots=0;
	
	for(c=0;c<note_candidate_count;c++)
	if(note_check[note_candidate[c]].roots>most_roots)
	most_roots=note_check[note_candidate[c]].roots;
	
// If this cosmically weird thing is the case, break, and it'll go and copy
// the currently initialized to "no note" tones array to the shared memory
// and return.
	if(most_roots==0)break;
	
// Now, finally, evaluate the candidates
	for(c=0;c<note_candidate_count;c++) {
		// If it has fewer than the most, ignore it
		if(note_check[note_candidate[c]].roots!=most_roots)continue;

		// This note wins for sure and others might as well
		tones[tone_index]=note_candidate[c];
		
		// But don't have a mistake and consider this one again
		// as we are making up to three passes here
		note_check[note_candidate[c]].roots=0;
		
		
		++tone_index;
		
		// Third come last served.  That's how it is.
		if(tone_index==3)break;
	}
	
	// And do it here too or there will be a rare segfault.
	if(tone_index==3)break;
	
	// Didn't find anything this pass?  Then we're done.
	if(tones[tone_index]==-1)break;
}

	// Finally, copy the working list to the shared memory for the synth
	// and go back.
	memcpy(front_info->tones,tones,3*sizeof(int));
}
	

// generate_sine_data: Generate a wave of the requested frequency at a low
// amplitude, marking phase reference points along the way so get_cycle() and
// its children can use it as a reference for analysis.
FREQ_FRONT *generate_sine_data(frequency,samples)
double frequency;
int samples;
{
static FREQ_FRONT	myfreq;	// This is always copied out immediately on return.

double d_period;
double step;
double angle;
double my_sin;	// double nothing is still nothing :(

int index;

double d_sample;

int phase_index;
int phase_size;

bool first_cycle;

	// Period is rate over frequency.
	d_period=rate/frequency;

	// Load required elements into return structure.
	myfreq.frequency=frequency;
	myfreq.period=(int)d_period;
	myfreq.sine_buf=malloc((samples+(int)d_period+1)*sizeof(short));

	angle=0;

	step=(2*M_PI)/d_period;

	phase_index=0;
	phase_size=(int)(d_period/MAX_PHASE);
	
	first_cycle=true;	// We have to set all the phase references
				// in the first cycle
			
	for(index=0;index<(samples+(int)d_period)+1;index++) {
		my_sin=sin(angle);
		d_sample=my_sin*SINE_CHECK_LEVEL;
		myfreq.sine_buf[index]=(short)d_sample;

////
if(first_cycle) { // if first cycle, set the phases.

// If we are at the point in the cycle to mark the next one...
if((index>=phase_index*phase_size)&&(phase_index<MAX_PHASE)) {

	// Each one will need an entire structure, already embedded in
	// the FREQ_FRONT type.
	bzero(&(myfreq.phase[phase_index]),sizeof(PHASE));

	// Mark the offset numerically and create a pointer to that place
	// in the buffer as well.
	myfreq.phase[phase_index].offset=index;
	myfreq.phase[phase_index].sine_buf=&(myfreq.sine_buf[index]);
	
	// record the period	
	myfreq.phase[phase_index].period=myfreq.period;
	
	// increment the index
	++phase_index;
}


} // first_cycle
///	
		angle+=step;
		if(angle>(2*M_PI)) {
			first_cycle=false;
			angle-=(2*M_PI);
		}
	}
	



	return(&myfreq);
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


// Print formatting of for tones on status line
char *tone_set_str(tone_set)
int	*tone_set;
{
static	char	tone_str[MISC_TEXT];

int	c;

	bzero(tone_str,MISC_TEXT);
	
	if((tone_set[0]==-1)&&(tone_set[1]==-1)&&(tone_set[2]==-1)) {
		strcpy(tone_str,"nothing");
		return(tone_str);
	}
	
	for(c=0;c<17;c++) tone_str[c]=' ';
	
	for(c=0;c<3;c++) {
		if((tone_set[c]<-1)||(tone_set[c]>11)) {
			tone_str[(c*6)+1]='n';
			tone_str[(c*6)+2]='f';
			tone_str[(c*6)+3]='g';
			continue;
		}
		if(tone_set[c]==-1) {
			tone_str[(c*6)+2]='-';
			continue;
		}
		if(strlen(note[tone_set[c]])==5)
			strncpy(&(tone_str[c*6]),note[tone_set[c]],5);
		else tone_str[(c*6)+2]=note[tone_set[c]][0];
	}
	return(tone_str);
}


// retime has two modes of operating, one which retimes the buffer to match
// the duration, and the other which rerates the buffer to 44100.  If
// duration has a non-zero value, it'll be the former, and rerate will be
// ignored. (Historical note from previous app I wrote that used that feature)

short *retime(wavbuf,duration,wav_header)
short *wavbuf;
int duration;
WAV_HEADER *wav_header;
{
double oldsize,newsize;	// these are in samples, not samples per channel
			// or bytes
double ratio;
double remain;
double wanted;

double collecter_l,collecter_r;

int index;

short *newbuf;

int index_old;
int index_new;


short *p;
int ch;

// Do we need this?
/*	
	if(wav_header->rate!=44100) {
		if(first)printf("Changing rate from %d to 44100\n",wav_header->rate);
		ch=wav_header->rate;
		wav_header->rate=44100;
		p=retime(wavbuf,0,wav_header,(double)44100/(double)ch,first);
		if(p==NULL) {
			printf("There was a memory problem in rerate\n");
//			return NULL;
			exit_cleanly();
		}
		wavbuf=p;		
	}
*/
//	if(duration) {
		
//		p=extend_beats(wavbuf,duration,wav_header);
	
//		if(p==NULL) printf("There is a memory problem\n");
//		else wavbuf=p;
		oldsize=wav_header->filesize/sizeof(short)/wav_header->channels;
		newsize=duration;
				
/*	}  else {
	
	newsize=
	(rerate*((wav_header->filesize/wav_header->channels)/sizeof(short)));
	}
		
	oldsize=(wav_header->filesize/wav_header->channels)/sizeof(short);

	if(newsize==oldsize) {
		if(first)printf("Retime: Nothing to do, beat file good to go\n");
		return(wavbuf);
	}	
*/
//	printf("Adjusting size from %d to %d\n",(int)oldsize,
//	(int)newsize);
	
	ratio=newsize/oldsize;
	
	remain=ratio;
	wanted=1;
	
	collecter_l=0;
	collecter_r=0;
	
	index_old=0;
	index_new=0;

	newbuf=malloc(newsize*wav_header->channels*sizeof(short));

	ch=wav_header->channels;

	for(;;) {
 				
		if(remain<wanted) {
			collecter_l+=remain*wavbuf[index_old*ch];
			if(ch==2)collecter_r+=remain*wavbuf[(index_old*2)+1];
			wanted-=remain;
			remain=ratio;
			++index_old;
			if(index_old==(int)oldsize)break;
			continue;
		}

		if(remain==wanted) {
		
			collecter_l+=remain*wavbuf[index_old*ch];
			if(ch==2)collecter_r+=remain*wavbuf[(index_old*2)+1];			

			newbuf[index_new*ch]=(short)collecter_l;
			if(ch==2)newbuf[(index_new*2)+1]=(short)collecter_r;
			
			++index_new;
			if(index_new==(int)newsize)break;
			
			++index_old;
			if(index_old==(int)oldsize)break;
			
			collecter_l=0;
			collecter_r=0;
			
			wanted=1;
			remain=ratio;
			
			continue;
		}

		collecter_l+=wanted*wavbuf[index_old*ch];
		if(ch==2)collecter_r+=wanted*wavbuf[(index_old*2)+1];
		
		remain-=wanted;
		
		newbuf[index_new*ch]=(short)collecter_l;
		if(ch==2)newbuf[(index_new*2)+1]=(short)collecter_r;
			
		++index_new;
		if(index_new==(int)newsize)break;
		
		wanted=1;

		collecter_l=0;
		collecter_r=0;
				
	}
	wav_header->filesize=newsize*sizeof(short)*wav_header->channels;
	free(wavbuf);
	return(newbuf);
}

// Write the processed drum samples in a temp file in /tmp, using a simplified
// header that contains the information synth needs about it and nothing else.
void write_beats_init(beats)
char *beats;
{
int	beats_wav_fd;
int	beats_raw_fd;
int	c;
char	*p;
int	beat;

	for(c=0,p=beats;*p;c++,p++) if(!isdigit(*p)) break;
	
	// This is probably meant to be a bpm, not a file
	if(c==strlen(beats)) return;
	
	if(beats!=NULL) if(strcmp(beats,"off")) {
		beats_wav_fd=open(beats,O_RDONLY);
		if(beats_wav_fd==-1) printf("Not able to open %s\n",beats);
		else {
	
/////
		sprintf(playcmd,DRUM_FMT,geteuid());
		beats_raw_fd=
	open(playcmd,O_WRONLY|O_CREAT|O_TRUNC,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
		if(beats_raw_fd==-1) printf("Not able to write tmpfile %s for beats\n",playcmd);
		else write_beat_dat(beats_wav_fd,beats_raw_fd);
/////		
		}
	}
}

// Look the the filename of the drum file and see if it has a valid bpm
// number embedded.
int get_file_bpm(name)
char	*name;
{
int	c;
int	this_bpm;

	if(strlen(name)<3)return(-1);

	this_bpm=-1;
		
	for(c=0;c<strlen(name);c++) {
		if(c==0) if(isdigit(name[c-1])) continue;
		if(!isdigit(name[c]))continue;
		if(!isdigit(name[c+1]))continue;
		
		if(!isdigit(name[c+2])) {
			this_bpm=atoi(&(name[c]));
			if((this_bpm>=BPM_MIN)&&(this_bpm<=BPM_MAX)) 
				return(this_bpm);	
			else {
				this_bpm=-1;
				continue;
			}
		}
		if(isdigit(name[c+3])) continue;
		
		this_bpm=atoi(&(name[c]));
		
		if((this_bpm>=BPM_MIN)&&(this_bpm<=BPM_MAX)) return(this_bpm);
		
		this_bpm=-1;
	}
	return(-1);	
}

// Take the drum file and make it loop exactly according to the bpm we're
// using
void write_beat_dat(wav_fd,raw_fd)
int wav_fd;
int raw_fd;
{
WAV_HEADER wav_header;
BEATS_HEADER raw_header;

short *wavbuf;

int c;

int chunk_size;

int chunk_size_target;

int resized;
int leftover;
	
int samples;

int new_file_size;

int	beats_file;

int	duration;

	
	if(read(wav_fd,&wav_header,WAV_HEADER_SIZE)<WAV_HEADER_SIZE) {
		printf("Can't read wave file header\n");
		close(wav_fd);
		close(raw_fd);
		return;
	}
	
	wav_header.filesize-=36;

//	printf("\nBeat file info:\nSize: %d\n",wav_header.filesize);
//	printf("Format: %d\n",wav_header.format);
//	printf("Channels: %d\n",wav_header.channels);
//	printf("Samples: %ld\n",(wav_header.filesize/sizeof(short))
//	/wav_header.channels);
	
//	printf("Rate: %d\n",wav_header.rate);
	
	if(wav_header.format!=1) {
		printf("Only pcm format for beats file supported, sorry\n");
		return;
	}

	samples=(wav_header.filesize/sizeof(short))/wav_header.channels;
	
	resized=samples;
	
	chunk_size=(wav_header.rate*60)/((f_bpm==-1)?bpm:f_bpm);
//	printf("File beat size: %d\n",chunk_size);
	
	if(((resized/chunk_size)*chunk_size)!=resized) {
		leftover=resized%chunk_size;
		resized=((resized/chunk_size)*chunk_size);
		
		if(leftover>(chunk_size/2)) resized+=chunk_size;
		
//		printf("Adjusted end from %d to %d\n",
//		samples,resized);
	}
	
	wav_header.filesize=resized*sizeof(short)*wav_header.channels;
	
	
	wavbuf=malloc(resized*sizeof(short)*wav_header.channels);
	
	
	if(wavbuf==NULL) {
		printf("Unable to allocate memory\n");
		close(wav_fd);
		close(raw_fd);
		exit_cleanly();	
	}
	bzero(wavbuf,resized*sizeof(short)*wav_header.channels);
	
	new_file_size=resized*sizeof(short)*wav_header.channels;
			
	if((c=read(wav_fd,wavbuf,new_file_size))<new_file_size)
	if(c!=wav_header.filesize)printf("Found only %d bytes of audio data and not the expected %d\n",c,wav_header.filesize);
	

	if((f_bpm!=-1)&&(a_bpm!=-1)&&(f_bpm!=a_bpm)) {
	
//	printf("Checking rate and time of beat file\n");
	
	
		chunk_size_target=(rate*60)/a_bpm;

		beats_file=resized/chunk_size;
		
//		printf("File: %d samples, %d beats\n",resized,beats_file);
		
		
		
		duration=beats_file*chunk_size_target;
		
//		printf("Resizsing to %d samples\n",duration);
		
		wavbuf=retime(wavbuf,duration,&wav_header);
	
		resized=duration;
		
		
	}
	
	raw_header.created_by=getpid();
	raw_header.rate=wav_header.rate;
	raw_header.stereo=(wav_header.channels==2)?true:false;
	raw_header.samples=resized;
	
	write(raw_fd,&raw_header,sizeof(BEATS_HEADER));
	write(raw_fd,wavbuf,resized*sizeof(short)*wav_header.channels);
		
//	printf("Wrote beats file for enlivener-synth\n");
	
	have_drums=true;
	
	close(wav_fd);
	close(raw_fd);
	
}

// Set a global flag to indicate the synth is ready for action.
void set_synth_state()
{
	if(front_info->reached_end) {
//	printf("\n\nReached desired end point and synth faded - exiting\n\n");
		exit_cleanly();
	}
	synth_ready=true;
}


// Set the terminal state to how we found it and exit.
void exit_cleanly()
{
	tcsetattr(STDIN_FILENO, TCSANOW, &old_attr);
	printf("\n\nThank you for playing with me!\n");

	exit(EXIT_SUCCESS);
	
}
	
// Search for a valid enlivener-synth executable in the list of directories
// in the PATH environment variable.
char *get_synth_exe()
{
char *path;
static char fname[MISC_TEXT];
char *p, *q;

struct stat st;

int stat_stat;

uid_t	my_euid;
gid_t	my_egid;

unsigned char relevant_bit;

	my_euid=geteuid();
	my_egid=getegid();
	
	path=getenv("PATH");
	bzero(fname,MISC_TEXT);
	
	if(path!=NULL) {
		p=path;
		for(;;) {
		
			bzero(fname,strlen(fname));
			q=fname;
					
			while((*p!=':')&&(*p!='\0')) {
				*q=*p;
				++p;
				++q;
			}
			
			sprintf(&(fname[strlen(fname)]),"/%s",SYNTH_EXECUTABLE);
		
			stat_stat=(stat(fname,&st));
			
			if(stat_stat==-1) {
				if(*p=='\0') break;
				else {
					++p;
					continue;
				}
			} else {
				
				if((st.st_mode&S_IFMT)!=S_IFREG) {
					if(*p=='\0') break;
					else {
						++p;
						continue;
					}
				}
				
				relevant_bit=S_IXOTH;
				if(my_egid==st.st_gid) relevant_bit=S_IXGRP;
				if(my_euid==st.st_uid) relevant_bit=S_IXUSR;

				if(((st.st_mode)&relevant_bit)==relevant_bit)
				return(fname);			
			}
				
			if(*p=='\0') break;
			
			++p;
		}
	}
	return(NULL);

}

// A helper function for the general purpose pivot sort that I wrote for
// another app, and included here.  The other app needed a zillion such
// functions but this wound up only needing one.  
unsigned char sort_phase(ph1,ph2)
PHASE *ph1;
PHASE *ph2;
{
	if(ph1->benefit>ph2->benefit) return(SORT_KEEP);
	if(ph1->benefit<ph2->benefit) return(SORT_SWAP);
	return(SORT_EQUAL);
}

// cancel_check()
//
// This function is the real meat of the matter.  It takes arguments "use_phase"
// which is a pointer to structure the holds information about a particular
// note/phase combination.  It checks to see how well the wave thus referenced
// cancels the input signal, as follows.
//
// It loops through the input buffer sample by sample and at each time index,
// checks to see how much closer to zero the test signal brings the input buffer.
// If it does tend to cancel signal, it adds by how much to a counter call "down".
// If it does the opposite, it adds the amount of signal amplification to a counter
// called "up".
//
// At the end of the loop, down is compared to up and if down is greater,
// the value of (down-up) is assigned to member "benefit" of the use_phase
// structure.  Otherwise, benefit is set to zero.  Finally, the value
// benefit is divided by the period of the test wave to remove unwarranted
// biases in comparison due to differing lengths.
//
// The value of use_phase->benefit is used by the parent function to sort
// the array and begin to identify the tone, but there's more work for it
// yet, described at that code block.
void cancel_check(use_phase,aud,len)
PHASE *use_phase;
short *aud;
int len;
{
int	c;

long down_p;
long up_p;
short	abs_aud;
int	avg;


short *sine;

unsigned long up;
unsigned long down;


	// A one-cycle buffer of the test wave starting from the appropriate
	// phase point
	sine=use_phase->sine_buf;
	
	up=0;
	down=0;

	// We initialize this inherited object because the parent functions
	// do all the sorting from this object and do what they need to do,
	// without cleaning up, since this assignment is the only part
	// of that cleanup that's needed
	use_phase->benefit=0;
		
	for(c=0;c<len;c++) {

	
		avg=(sine[c]+aud[c]);
		// Because we're testing wave *cancellation*

// Handle the case where the test wave and the audio signal are on the same
// size of zero.  
if(	((avg>=0)&&(aud[c]>=0)) || ((avg<=0)&&(aud[c]<=0)) ) {
		avg=abs(avg);
		abs_aud=abs(aud[c]);
	
		if(abs_aud>=avg) down+=(unsigned long)(abs_aud-avg);
		else up+=(unsigned long)(avg-abs_aud);

// Handle the case where they are on opposite sides of zero.
} else {
	
		// Create absolute value versions of the two
		down_p=(long)abs(aud[c]);
		up_p=(long)abs(avg);
		
		// Do the simple arithmetic check as in the other case
		down_p-=up_p;
		
		// Log the benefit or penalty as appropriate.
		if(down_p>=0) down+=down_p;
		else up+=(abs(down_p));
		
}
///		
	}

	// Assign the value of the overall benefit, or zero if no benefit or
	// overall penalty.
	use_phase->benefit=(down>up)?(down-up):0;

	// Adjust by wave period so lower notes are not inappropriately 
	// weighted.
	use_phase->benefit/=len;
}


// Simple controller function to loop through all the note/phase combinations
// to run cancel_check()
void get_phase_variance(use_phase,count,aud,len)
PHASE **use_phase;
int count;
short *aud;
int len;
{
int c;

	for(c=0;c<count;c++) {
		if(use_phase[c]->period>len) {
			use_phase[c]->benefit=0;
			continue;
		}	
		cancel_check(use_phase[c],aud,len);
	}
}


