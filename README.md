# Enlivener
## A Time-Domain Live Guitar Tracker for Linux

Enlivener listens to an incoming guitar signal, discerns pitches from it, and creates a live accompaniment such that it sounds like finished music.

The accompaniment as coded includes the sequencing of an optionally provided external drum track, a complex wave set designed to imitate a bass guitar, and a series of pleasant tinkling sounds in octaves near the top of the guitar range. The latter two are made to sound consonant with what the guitarist is playing at any given moment.

Enlivener is set to work with a fixed tempo given on the command line, ranging from 120 to 240, defaulting to the former. The tempo, once set, is the same for the entire execution run.

The development platform was Ubuntu with this kernel: `6.8.0-138-generic`

My machine is a Lenovo ThinkPad, described by the `inxi` utility as follows:

```text
CPU: dual core Intel Core i7-6600U (-MT MCP-)
speed/min/max: 1124/400/3400 MHz Kernel: 6.8.0-138-generic x86_64 Up: 38d 54m
Mem: 5033.9/7797.3 MiB (64.6%) Storage: 357.72 GiB (40.4% used) Procs: 277
Shell: Bash inxi: 3.3.13
```

I am told this machine is basically a door stop. My app runs on it.

## 📺 Live Proof

Video demonstrations can be found on this channel: [https://www.youtube.com/@enlivener-d7f](https://www.youtube.com/@enlivener-d7f)

Most videos that show a trans woman guitarist in the thumbnail feature the app as well. If you hit one of the ones that doesn't, just pick a different one.

## 🧠 How it Works

The app is split into two components: `enlivener` (the tracking engine) and `enlivener-synth` (the audio output engine). You invoke `enlivener` on the command line, which spawns the synth process and establishes a compact shared memory block between the two processes.

`enlivener` starts the synth and waits for it to pre-construct the complex tones that will be used in the output waves. Once the synth notifies `enlivener` that it is finished, `enlivener` presents the copyright blurb and the message, "Ok, start playing."

When it hears that you have—or sometimes if there's string noise, and sometimes if you breathe at it—it begins generating an accompaniment.

## ⚡ Time-Domain Extraction vs. Frequency-Domain Latency

Unlike standard modern audio software that relies on computationally heavy Fourier Transforms (FFT) which introduce unavoidable latency bottlenecks, Enlivener bypasses the frequency domain entirely.  (That's what Gemini said, not me, don't get mad Mr. Fourier.  It said 'heavy' too, too, I'm not the one calling you fat here.)

The core extraction engine processes raw amplitude vectors directly in the time domain using a monophonic wave-cancellation analysis framework (`cancel_check()`). This achieves the speed needed to keep up to an audio stream even on a "door stop" computer as described in the intro.

It's probably a little much to expect it to run on Charles Babbage's Analytical Engine, even if Linux has been ported to it.  If Ada were still with us anything would be possible but as it is, forget about it.



## 🔄 Behind the Scenes: The Front-End Engine Loop

After initializing, `enlivener` runs a in a loop until interrupted. In that loop, it does the following:

*   **Determines the current fundamental note:** Executes the raw time-domain wave-cancellation function `cancel_check()`. The internal mechanics of this process are meticulously documented in the `enlivener.c` source file. The extracted pitch is committed directly to the asynchronous shared memory block and placed at the top of a 'stack' of harmonic data it's holding for the synth so that it may do more interesting things than just repeating the copied note, and still sound consonant.
*   **Prints a status line:** Outputs a real-time tracking line displaying the selected fundamental pitch alongside the active state of the harmonic stack.

## 🎹 Behind the Scenes: The Synth Loop

> "The enlivener-synth.c process acts as a non-novel, unpolished consumer draining the zero-copy shared memory block." - Google Gemini

Gemini is trying to say that the synth doesn't have any new tricks in it. I'm sure you'll all recognize it as bog standard stuff, so it's probably fine that I didn't comment it really meticulously.

It executes a continuous loop that terminates automatically when the front-end engine exits:

*   **Reads shared memory:** Checks the asynchronous shared memory block to pull the active pitch vector.
*   **Constructs the audio block:** Compiles a half bar of real-time audio at the specified tempo—mixing the sequenced wave sets and the external drum track—and pumps it directly to the PulseAudio output device. A sort of "harmony stack" is maintained to help this process for the purpose of harmonic variety, rather than boringly try to just double the guitar.
*   **Verifies engine persistence:** Confirms the front-end process is actively running and performs a clean teardown if it has terminated.

## 🛠️ Build Requirements and Instructions

Pretty well any modern Linux and gcc and PC with at least the specs I talked about in the intro would have to do it, I would think. You have to install pulse and pulse-simple libraries, and go like this:

```bash
cc enlivener.c -lm -lpulse -lpulse-simple -o enlivener
cc enlivener-synth.c -lm -lpulse -lpulse-simple -o enlivener-synth
```

How your shell finds `enlivener` is up to you. `enlivener` needs to start `enlivener-synth` though, so you have a choice of compiling `enlivener` to look for a specific path only or look in the `$PATH` environment variable.

Look in the `enlivener.h` file for a string constant called `SYNTH_EXECUTABLE`. If you want a specific pathname for it, just have the constant include a '/' character, like `"./enlivener-synth"` or `"/usr/local/bin/enlivener-synth"`. If you want it to look in the path, just have it be the basename `"enlivener-synth"`.

## 🚀 Running It

```bash
enlivener drums.wav b120 100 100
```

Where `drums.wav` is a precisely trimmed drum track at the desired tempo. Short loops provide optimal stability, though extended tracks are supported. The `b120` parameter dictates the execution tempo; replace `120` with any integer from 120 to 240 BPM. The optional trailing integers define volume scaling for the bass and drum outputs, respectively, ranging from 0 to 100.

If you find the app is running, displaying the "Ok, start playing" message but not responding to the guitar, and you know the "mic" (guitar) is not muted or anything like that, go to your pulse mixer and try adjusting something called "enlivener analysis."  I've got my sound card input (guitar) almost pegged, and the "enlivener analysis" slider at about 3/4.
## 📜 On Myself and Why I'm Doing This

I was mostly a construction worker during my working life and just happen to know C from reading K&R in 1991, and having that as a hobby since then, being as I also have been Linuxing since 94, starting with Slackware. That's about all I have in common with my idea of the usual folks in the general developer community. I'm not really culturally like you guys. I don't dislike you or anything but I don't really give a shit about your "Holy Wars" or any bullshit like that, other than a firm opinion that programming languages other than C are strictly for children, a very usual stance among you guys I'm guessing.

(It's a joke, calm down!)
## ⚖️ Licensing & Derivatives (CC BY-NC-SA 4.0)

Project Enlivener is published under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International Public License (CC BY-NC-SA 4.0).

The full legal code of the license is incorporated by reference and can be reviewed in its entirety at:  
[https://creativecommons.org/licenses/by-nc-sa/4.0/](https://creativecommons.org/licenses/by-nc-sa/4.0/)

## 🛡️ Intellectual Property & Prior Art Shield

This public repository functions as an immutable, open-source broadcast establishing definitive Prior Art. The publication of these time-domain extraction mechanisms permanently bars commercial entities, software corporations, or third-party proprietary developers from securing patents against these specific algorithmic pathways.

Commercial utilization of this architecture—including commercial audio plugins, closed-source derivatives, or integration into paid tracking software—is strictly prohibited under the terms of this license.

## ⚠️ Disclaimer of Liability & Statement of Boundaries and General Attitude


This project sort of got rushed out the door and so while it works and has been working to my satisfaction for some months now, there's bound to be some buried treasure.  I approached this as what I am - a musician who happens to be able to code.  At times when I got something to work, I got back to playing my guitar and I really didn't refactor all that much.  I'm also not a dev by trade and I'm not that interested in learning the conventions thereof - I don't care if the way I did things is "deprecated", I don't care if the logic of this or that looks weird to you and I don't even care if it has obvious code errors, as long as they don't crash it.  It sounds the way I want it to and that's what's important to me.

This software is shared effectively for free under the stated license and is not guaranteed to be suitable for any specific purpose. It probably makes a really shitty vegetable peeler for example. That being said, I'm planning at the time of this writing to lock my own version of Enlivener to other-party revisions, but within the terms of the license, fork away and have at 'er. I'm generally amenable to questions from polite people if the volume of such isn't overwhelming.  If I like some of the forks I might very well play something to your version on youtube, which is as of yet not a big deal, but there it is for what it's worth.  

I am a trans woman.  Here's the deal: I don't tell you what to think of that or trans in general, and you don't be rude to me with masculinizing words.  Fair?

What a great note to end on.  ROCK ON, I meant to say!  ;)


## dev_questions.txt

My notebook of stuff that I was going to address before I pushed this out the door, before I decided that was higher priority than I'd felt at first and published this partially refactored version.




