/** Minicomputer
 * industrial grade digital synthesizer
 *
 * Copyright 2007 Malte Steiner
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <jack/jack.h>
//#include <jack/midiport.h> // later we use the jack midi ports to, but not this time
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <lo/lo.h>
#include <string.h>
#include <alsa/asoundlib.h>
#include <pthread.h>
// some common definitions
#include "../common.h" 
snd_seq_t *open_seq();
 snd_seq_t *seq_handle;
  int npfd;
  struct pollfd *pfd;
//void midi_action(snd_seq_t *seq_handle);


snd_seq_t *open_seq() {

  snd_seq_t *seq_handle;
  int portid;

  if (snd_seq_open(&seq_handle, "hw", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
    fprintf(stderr, "Error opening ALSA sequencer.\n");
    exit(1);
  }
  snd_seq_set_client_name(seq_handle, "Minicomputer");
  if ((portid = snd_seq_create_simple_port(seq_handle, "Minicomputer",
            SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
            SND_SEQ_PORT_TYPE_APPLICATION)) < 0) {
    fprintf(stderr, "Error creating sequencer port.\n");
    exit(1);
  }
  return(seq_handle);
}


//  gcc -o synthesizer synth2.c -ljack -ffast-math -O3 -march=k8 -mtune=k8 -funit-at-a-time -fpeel-loops -ftracer -funswitch-loops -llo -lasound
int done = 0;
void error(int num, const char *m, const char *path); 
int generic_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, void *user_data); 
int foo_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, void *user_data); 

int quit_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, void *user_data);



jack_port_t* inbuf;
jack_client_t *client;
#define _PARACOUNT 139
#define _MODCOUNT 18
#define _WAVECOUNT 20
#define _CHOICEMAX 16
#define _MULTITEMP 8
#define TableSize 4096
#define tabM 4095
#define tabF 4096.f

jack_port_t   *port[_MULTITEMP + 4]; // _multitemp * ports + 2 mix and 2 aux
float phase[_MULTITEMP][3];//=0.f;
float parameter[_MULTITEMP][_PARACOUNT];
float modulator[_MULTITEMP][_MODCOUNT];
unsigned int choice[_MULTITEMP][_CHOICEMAX];
float midi2freq [128],midif[_MULTITEMP];
float table [_WAVECOUNT][TableSize];
float EG[_MULTITEMP][7][8];
int EGrepeat[_MULTITEMP][7];
unsigned int EGtrigger[_MULTITEMP][7];
unsigned int EGstate[_MULTITEMP][7];
unsigned int currentvoice = 0;
float sampleRate=48000.0f;
float tabX = 4096.f / 48000.0f;
float srate = 3.145f/ 48000.f;
float osc1,osc2;
float high[_MULTITEMP][3],band[_MULTITEMP][3],low[_MULTITEMP][3],temp=0,f[_MULTITEMP][3],q[_MULTITEMP][3],v[_MULTITEMP][3],lfo,tf,faktor[_MULTITEMP][3];
int i;
unsigned int lastnote[_MULTITEMP];
jack_nframes_t 	bufsize;

static inline float Oscillator(float frequency,int wave,float *phase)
{
    int i = (int) *phase;
	i%=TableSize;
	//if (i>tabM) i=tabM;
	if (i<0) i=tabM;
    *phase += tabX * frequency;

    if(*phase >= tabF)
    {
   		 *phase -= tabF;
    }


        if(*phase < 0.f)
                {
                	*phase += tabF;
                }
        return table[wave][i] ;
}
//inline float filter (float input,float f, float q)
//{
//
//
//}
static inline void egStart (unsigned int voice,unsigned int number)
{
	     EGtrigger[voice][number]=1;
	     EG[voice][number][0] = 1.f; // triggerd
		 EG[voice][number][5] = 1.f; // target
         EG[voice][number][7] = 0.0f;// state
         EGstate[voice][number] = 0;// state
		 //printf("start %i", voice);
}
static inline void egStop (unsigned int voice,unsigned int number)
{
	    // if (EGrepeat[voice][number] == 0) 
	    EGtrigger[voice][number] = 0; // triggerd
		 EGstate[voice][number] = 0; // target
		// printf("stop %i", voice);
}
static inline float egCalc (unsigned int voice, unsigned int number)
{
	/* EG[x] x:
	 * 0 = trigger
	 * 1 = attack
	 * 2 = decay
	 * 3 = sustain
	 * 4 = release
	 * 5 = target
	 * 6 = state
	 */
	if (EGtrigger[voice][number] != 1)
	{
		if (EGstate[voice][number] == 1){ // attack
			 EG[voice][number][6] += EG[voice][number][1];
			 if (EG[voice][number][6]>=1.0f)
			 {
			 	EG[voice][number][6]=1.0f;
			 	EGstate[voice][number]=2;
			 }
		}
		else if (EGstate[voice][number] == 2){ // decay
			if (EG[voice][number][6]>EG[voice][number][3])
			{
				EG[voice][number][6] -= EG[voice][number][2];
			}
			else 
			{
				if (EGrepeat[voice][number]==0)
				{
					EGstate[voice][number]=3;
				}
				else
				{
					egStop(voice,number);
				}
			}
		}
		else if ((EGstate[voice][number] == 0) && (EG[voice][number][6]>0.0f))
		{
		    /* release */
		    
		    EG[voice][number][6] -= EG[voice][number][4];//*EG[number][6];
		    if (EG[voice][number][6]<0.0f) 
		    {	
		    	EG[voice][number][6]=0.0f;
		    	if (EGrepeat[voice][number]==0)
				{
					EGstate[voice][number]=4; // released
				}
				else
				{
					egStart(voice,number);
				}
		    }
		}
//		if (EG[number][5] == 1.0f){
//		    /* attack */
//		   
//		    EG[number][6] += EG[number][1]*(1.0f - EG[number][6]);
//		    EG[number][5] = (EG[number][6] > 1.0f) ? EG[number][3] : 1.0f;
//		}
//		else if ((EG[number][5] == 0.0f) && (EG[number][6]>0.0f))
//		{
//		    /* release */
//		    
//		    EG[number][6] -= EG[number][4];//*EG[number][6];
//		    if (EG[number][6]<0.0f) EG[number][6]=0.0f;
//		}
//		else{
//		    /* decay */
//		    EG[number][6] -= EG[number][2]*(EG[number][6]-EG[number][3]);
//		}
	}
	else
	{
		if (EGtrigger[voice][number] == 1) // declick ramp down processing
		{
			
			EG[voice][number][6] -= 0.005f;
			if (EG[voice][number][6]<0.0f) 
			{  
				if  (EG[voice][number][7]< EG[voice][number][6])
				{
			   	 EG[voice][number][7] += EG[voice][number][1]*(1.0f - EG[voice][number][7]);
				}
			    else
			    {
					EGtrigger[voice][number] = 0;
					EGstate[voice][number] = 1;
			    }
				
			}
		}
		else if (EG[voice][number][0] == 2.f) // actual start
		{
			EG[voice][number][0] = 0.f;
			
			EG[voice][number][0] = 1.f; // triggerd
		    EGstate[voice][number] = 1; // target
            EG[voice][number][6] = 0.0f;// state
		}
		
	}
	return EG[voice][number][6];
}
//float d0,d1,d2,c1;

/* this is the heart of the client. the process callback. 
 * this will be called by jack every process cycle.
 * jack provides us with a buffer for out output port, 
 * which we can happily write into. inthis case we just 
 * fill it with 0's to produce.... silence! not to bad, eh? */
int process(jack_nframes_t nframes, void *arg) {
float tf,ta1,ta2,morph,mo,mf;
unsigned int index;

/*jack_midi_port_info_t* info;
	void* buf;
	jack_midi_event_t ev;
	
	
	buf = jack_port_get_buffer(inbuf, bufsize);
	info = jack_midi_port_get_info(buf, bufsize);
	for(index=0; index<info->event_count; ++index)
	{
		jack_midi_event_get(&ev, buf, index, nframes);
	}
*/
	
		

	/* so we do it :) */
	for (index = 0; index < nframes; ++index) {
	/* this function returns a pointer to the buffer where 
     * we can write our frames samples */

		
for (currentvoice=0;currentvoice<_MULTITEMP;++currentvoice)
{		
	float *buffer = (float*) jack_port_get_buffer(port[currentvoice], nframes);
		buffer[index]=0.0f;

// calc the modulators
modulator [currentvoice][8] =egCalc(currentvoice,1);
modulator [currentvoice][9] =egCalc(currentvoice,2);
modulator [currentvoice][10]=egCalc(currentvoice,3);
modulator [currentvoice][11]=egCalc(currentvoice,4);
modulator [currentvoice][12]=egCalc(currentvoice,5);
modulator [currentvoice][13]=egCalc(currentvoice,6);
modulator [currentvoice][14]=Oscillator(parameter[currentvoice][90],choice[currentvoice][12],&phase[currentvoice][3]);

tf = parameter[currentvoice][1]*parameter[currentvoice][2]+(midif[currentvoice]*(1.0f-parameter[currentvoice][2])*parameter[currentvoice][3])+
		parameter[currentvoice][4]*parameter[currentvoice][5]*modulator[currentvoice][choice[currentvoice][0]]+
		parameter[currentvoice][7]*modulator[currentvoice][choice[currentvoice][1]];
//tf/=3.f;		
ta1 = parameter[currentvoice][9]*modulator[currentvoice][choice[currentvoice][3]]+parameter[currentvoice][11]*modulator[currentvoice][choice[currentvoice][2]];
//ta/=2.f;
osc1 = Oscillator(tf,choice[currentvoice][4],&phase[currentvoice][1]);
modulator[currentvoice][3]=osc1*(parameter[currentvoice][13]+parameter[currentvoice][13]*ta1);

tf = parameter[currentvoice][16]*parameter[currentvoice][17]+(midif[currentvoice]*(1.0f-parameter[currentvoice][17])*parameter[currentvoice][18])+
	parameter[currentvoice][15]*parameter[currentvoice][19]*modulator[currentvoice][choice[currentvoice][6]]+
	parameter[currentvoice][21]*modulator[currentvoice][choice[currentvoice][7]];
//tf/=3.f;		
ta2 = parameter[currentvoice][23]*modulator[currentvoice][choice[currentvoice][8]] + parameter[currentvoice][25]*modulator[currentvoice][choice[currentvoice][9]];
//ta/=2.f;
osc2 = Oscillator(tf,choice[currentvoice][5],&phase[currentvoice][2]);
modulator[currentvoice][4] = osc2 * (parameter[currentvoice][28]+parameter[currentvoice][28]*ta2);

temp=(osc1*(parameter[currentvoice][14]+parameter[currentvoice][14]*ta1)+osc2*(parameter[currentvoice][29]+parameter[currentvoice][29]*ta2))*0.5f;	
/* filter settings*/
mf = ( (1.f-(parameter[currentvoice][38]*modulator[currentvoice][ choice[currentvoice][10]]))+(1.f-parameter[currentvoice][48]*modulator[currentvoice][ choice[currentvoice][11]]) );
mo = parameter[currentvoice][56]*mf;
if (mo<0.f) mo = 0.f;
else if (mo>1.f) mo = 1.f;
morph=(1.0f-mo);

tf= (srate * (parameter[currentvoice][30]*morph+parameter[currentvoice][33]*mo) );
f[currentvoice][0] = 2.f * tf - (tf*tf*tf) * 0.1472725f;// / 6.7901358;

tf= (srate * (parameter[currentvoice][40]*morph+parameter[currentvoice][43]*mo) );
f[currentvoice][1] = 2.f * tf - (tf*tf*tf)* 0.1472725f; // / 6.7901358;;

tf = (srate * (parameter[currentvoice][50]*morph+parameter[currentvoice][53]*mo) );

f[currentvoice][2] = 2.f * tf - (tf*tf*tf) * 0.1472725f;// / 6.7901358; 
q[currentvoice][0] = parameter[currentvoice][31]*morph+parameter[currentvoice][34]*mo;
q[currentvoice][1] = parameter[currentvoice][41]*morph+parameter[currentvoice][44]*mo;
q[currentvoice][2] = parameter[currentvoice][51]*morph+parameter[currentvoice][54]*mo;

v[currentvoice][0] = parameter[currentvoice][32]*morph+parameter[currentvoice][35]*mo;
v[currentvoice][1] = parameter[currentvoice][42]*morph+parameter[currentvoice][45]*mo;
v[currentvoice][2] = parameter[currentvoice][52]*morph+parameter[currentvoice][55]*mo;

low[currentvoice][0] = low[currentvoice][0] + f[currentvoice][0] * band[currentvoice][0];
high[currentvoice][0] = q[currentvoice][0] * temp - low[currentvoice][0] - q[currentvoice][0]*band[currentvoice][0];
band[currentvoice][0]= f[currentvoice][0] * high[currentvoice][0] + band[currentvoice][0];


low[currentvoice][1] = low[currentvoice][1] + f[currentvoice][1] * band[currentvoice][1];
high[currentvoice][1] = q[currentvoice][1] * temp - low[currentvoice][1] - q[currentvoice][1]*band[currentvoice][1];
band[currentvoice][1]= f[currentvoice][1] * high[currentvoice][1] + band[currentvoice][1];


low[currentvoice][2] = low[currentvoice][2] + f[currentvoice][2] * band[currentvoice][2];
high[currentvoice][2] = q[currentvoice][2] * temp - low[currentvoice][2] - q[currentvoice][2]*band[currentvoice][2];
band[currentvoice][2]= f[currentvoice][2] * high[currentvoice][2] + band[currentvoice][2];
modulator[currentvoice] [7] = low[currentvoice][0]*v[currentvoice][0]+band[currentvoice][1]*v[currentvoice][1]+band[currentvoice][2]*v[currentvoice][2];

buffer[index] = modulator[currentvoice][7] *egCalc(currentvoice,0)*parameter[currentvoice][101];///_MULTITEMP;


		//buffer[index] = Oscillator(50.2f,&phase1) * 0.5f;
//Initialization done here is the oscillator loop
                
       }
}
	return 1;
}

/* a flag which will be set by our signal handler when 
 * it's time to exit */
int quit = 0;

/* the signal handler */
void signalled(int signal) {
	quit = 1;
}
void init ()
{
unsigned int i,k;
for (k=0;k<_MULTITEMP;k++)
{
	EG[k][0][1]=0.01f;
	EG[k][0][2]=0.01f;
	EG[k][0][3]=1.0f;
	EG[k][0][4]=0.0001f;
	EGtrigger[k][0]=0;
  
	parameter[k][30]=100.f;
	parameter[k][31]=0.5f;
	parameter[k][33]=100.f; 
	parameter[k][34]=0.5f;
	parameter[k][40]=100.f;
	parameter[k][41]=0.5f;
	parameter[k][43]=100.f; 
	parameter[k][44]=0.5f;
	parameter[k][50]=100.f;
	parameter[k][51]=0.5f;
	parameter[k][53]=100.f; 
	parameter[k][54]=0.5f;
	
	for (i=0;i<3;++i) 
	{
		low[k][i]=0;
		high[k][i]=0;
	}
}
float PI=3.145;
float increment = (float)(PI*2) / (float)TableSize;
float x = 0.0f;
float tri = -0.9f;
// calculate wavetables
for (i=0; i<TableSize; i++)
{
			table[0][i] = (float)((float)sin(x+(
				(float)2.0f*(float)PI)));
			x += increment;
			table[1][i] = (float)i/tabF*2.f-1.f;// ramp up
			
			table[2][i] = 0.9f-(i/tabF*1.8f-0.5f);// tabF-((float)i/tabF*2.f-1.f);//ramp down
			
			if (i<TableSize/2) 
			{ 
				tri+=(float)1.f/TableSize*3.f; 
				table[3][i] = tri;
				table[4][i]=0.9f;
			}
			else
			{
				 tri-=(float)1.f/TableSize*3.f;
				 table[3][i] = tri;
				 table[4][i]=-0.9f;
			}
			table[5][i] = 0.f;
			table[6][i] = 0.f;
			if (i % 2 == 0)
				table[7][i] = 0.9f;
			else table [7][i] = -0.9f;
			table[8][i]=(float) (
			((float)sin(x+((float)2.0f*(float)PI))) +
			((float)sin(x*2.f+((float)2.0f*(float)PI)))+
			((float)sin(x*3.f+((float)2.0f*(float)PI)))+
			((float)sin(x*4.f+((float)2.0f*(float)PI)))*0.9f+
			((float)sin(x*5.f+((float)2.0f*(float)PI)))*0.8f+
			((float)sin(x*6.f+((float)2.0f*(float)PI)))*0.7f+
			((float)sin(x*7.f+((float)2.0f*(float)PI)))*0.6f+
			((float)sin(x*8.f+((float)2.0f*(float)PI)))*0.5f
			) / 8.0f;	
			table[9][i]=(float) (
			((float)sin(x+((float)2.0f*(float)PI))) +
			((float)sin(x*3.f+((float)2.0f*(float)PI)))+
			((float)sin(x*5.f+((float)2.0f*(float)PI)))+
			((float)sin(x*7.f+((float)2.0f*(float)PI)))*0.9f+
			((float)sin(x*9.f+((float)2.0f*(float)PI)))*0.8f+
			((float)sin(x*11.f+((float)2.0f*(float)PI)))*0.7f+
			((float)sin(x*13.f+((float)2.0f*(float)PI)))*0.6f+
			((float)sin(x*15.f+((float)2.0f*(float)PI)))*0.5f
			) / 8.0f;
			
printf("%f ",table[8][i]);

}
table[5][0] = -0.9f;
table[5][1] = 0.9f;

table[6][0] = -0.2f;
table[6][1] = -0.6f;
table[6][2] = -0.9f;
table[6][3] = -0.6f;
table[6][4] = -0.2f;
table[6][5] = 0.2f;
table[6][6] = 0.6f;
table[6][7] = 0.9f;
table[6][8] = 0.6f;
table[6][9] = 0.2f;
sampleRate = 48000.0f;    //  %Sample rate
/*
float pi = 3.145f;
float oscfreq = 1000.0; //%Oscillator frequency in Hz
c1 = 2 * cos(2 * pi * oscfreq / Fs);
//Initialize the unit delays
d1 = sin(2 * pi * oscfreq / Fs);  
d2 = 0;*/
//Initialization done here is the oscillator loop
//% which generates a sinewave



// miditable for notes to frequency
for (i = 0;i<128;++i) midi2freq[i] = 8.1758f * pow(2,(i/12.f));

}
void *midiprocessor(void *handle) {
	struct sched_param param;
	int policy;
  snd_seq_t *seq_handle = (snd_seq_t *)handle;
	pthread_getschedparam(pthread_self(), &policy, &param);

	policy = SCHED_FIFO;
	param.sched_priority = 95;

	pthread_setschedparam(pthread_self(), policy, &param);

/*
if (poll(pfd, npfd, 100000) > 0) 
		{
    	  midi_action(seq_handle);
		} */
		
  snd_seq_event_t *ev;

  do {
   while (snd_seq_event_input(seq_handle, &ev))
   {
    switch (ev->type) {
      case SND_SEQ_EVENT_CONTROLLER:
      { 
        fprintf(stderr, "Control event on Channel %2d: %2d %5d       \r",
                ev->data.control.channel,  ev->data.control.param,ev->data.control.value);
        if  (ev->data.control.param==1)   
        	  modulator[ev->data.control.channel][ 16]=(float)ev->data.control.value/127.f;
        	  else 
        	  if  (ev->data.control.param==12)   
        	  modulator[ev->data.control.channel][ 17]=(float)ev->data.control.value/127.f;
        break;
      }
      case SND_SEQ_EVENT_PITCHBEND:
      {
         fprintf(stderr,"Pitchbender event on Channel %2d: %5d   \r", 
                ev->data.control.channel, ev->data.control.value);
                if (ev->data.control.channel<_MULTITEMP)
               	 modulator[ev->data.control.channel][2]=(float)ev->data.control.value/8192.f;
        break;
      }   
      case SND_SEQ_EVENT_CHANPRESS:
      {
         fprintf(stderr,"touch event on Channel %2d: %5d   \r", 
                ev->data.control.channel, ev->data.control.value);
                if (ev->data.control.channel<_MULTITEMP)
               	 modulator[ev->data.control.channel][ 15]=(float)ev->data.control.value/127.f;
        break;
      }
      case SND_SEQ_EVENT_NOTEON:
      {   
      	unsigned int c = ev->data.note.channel;
      	if (c <_MULTITEMP)
                if (ev->data.note.velocity>0)
                {
                lastnote[c]=ev->data.note.note;	
                midif[c]=midi2freq[ev->data.note.note];
                modulator[c][0]=ev->data.note.note/127.f;
                modulator[c][1]=(float)ev->data.note.velocity/127.f;
                egStart(c,0);
                if (EGrepeat[c][1] == 0)egStart(c,1);
                if (EGrepeat[c][2] == 0)egStart(c,2);
                if (EGrepeat[c][3] == 0)egStart(c,3);
               	if (EGrepeat[c][4] == 0) egStart(c,4);
               	if (EGrepeat[c][5] == 0) egStart(c,5);
               	if (EGrepeat[c][6] == 0) egStart(c,6);
               
        fprintf(stderr, "Note On event on Channel %2d: %5d       \r",
                c, ev->data.note.note);
		        break;  
                }
      }      
      case SND_SEQ_EVENT_NOTEOFF: 
      {
      	unsigned int c = ev->data.note.channel;
        fprintf(stderr, "Note Off event on Channel %2d: %5d      \r",         
                c, ev->data.note.note);
               if  (c <_MULTITEMP)
               if (lastnote[c]==ev->data.note.note)
               {
                egStop(c,0);  
               	if (EGrepeat[c][1] == 0) egStop(c,1);  
                if (EGrepeat[c][2] == 0) egStop(c,2); 
               	if (EGrepeat[c][3] == 0) egStop(c,3); 
               	if (EGrepeat[c][4] == 0) egStop(c,4);  
                if (EGrepeat[c][5] == 0) egStop(c,5);  
               	if (EGrepeat[c][6] == 0) egStop(c,6);
               }    
        break;       
    } 
    }
    snd_seq_free_event(ev);
   }
  } while (1==1);//(snd_seq_event_input_pending(seq_handle, 0) > 0);
  return 0;
}
int main() {
// ------------------------ midi init ---------------------------------
  pthread_t midithread;
  seq_handle = open_seq();
  npfd = snd_seq_poll_descriptors_count(seq_handle, POLLIN);
  pfd = (struct pollfd *)alloca(npfd * sizeof(struct pollfd));
  snd_seq_poll_descriptors(seq_handle, pfd, npfd, POLLIN);
    
    // create the thread and tell it to use Midi::work as thread function
	int err = pthread_create(&midithread, NULL, midiprocessor,seq_handle);
// ------------------------ OSC Init ------------------------------------   
  /* start a new server on port definied in _OSCPORT */
    lo_server_thread st = lo_server_thread_new(_OSCPORT, error);

    /* add method that will match any path and args */
    lo_server_thread_add_method(st, "/Minicomputer/choice", "iii", generic_handler, NULL);

    /* add method that will match the path /foo/bar, with two numbers, coerced
     * to float and int */
    lo_server_thread_add_method(st, "/Minicomputer", "iif", foo_handler, NULL);

    /* add method that will match the path /quit with no args */
  //  lo_server_thread_add_method(st, "/quit", "", quit_handler, NULL);
	
    lo_server_thread_start(st);
   





	/* setup our signal handler signalled() above, so 
	 * we can exit cleanly (see end of main()) */
	signal(SIGINT, signalled);

	init();
	/* naturally we need to become a jack client :) */
	client = jack_client_new("Minicomputer");
	if (!client) {
		printf("couldn't connect to jack server. Either it's not running or the client name is already taken\n");
		exit(1);
	}

	/* we register an output port and tell jack it's a 
	 * terminal port which means we don't 
	 * have any input ports from which we could somhow 
	 * feed our output */
	port[0] = jack_port_register(client, "output1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput|JackPortIsTerminal, 0);
	port[1] = jack_port_register(client, "output2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput|JackPortIsTerminal, 0);
	port[2] = jack_port_register(client, "output3", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput|JackPortIsTerminal, 0);
	port[3] = jack_port_register(client, "output4", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput|JackPortIsTerminal, 0);
	port[4] = jack_port_register(client, "output5", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput|JackPortIsTerminal, 0);
	port[5] = jack_port_register(client, "output6", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput|JackPortIsTerminal, 0);
	port[6] = jack_port_register(client, "output7", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput|JackPortIsTerminal, 0);
	port[7] = jack_port_register(client, "output8", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput|JackPortIsTerminal, 0);
	
	port[8] = jack_port_register(client, "mix out left", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput|JackPortIsTerminal, 0);
	port[9] = jack_port_register(client, "mix out right", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput|JackPortIsTerminal, 0);
	port[10] = jack_port_register(client, "aux out 1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput|JackPortIsTerminal, 0);
	port[11] = jack_port_register(client, "aux out 2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput|JackPortIsTerminal, 0);
	//inbuf = jack_port_register(client, "in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	/* jack is callback based. That means we register 
	 * a callback function (see process() above)
	 * which will then get called by jack once per process cycle */
	jack_set_process_callback(client, process, 0);
	bufsize = jack_get_buffer_size(client);
	/* tell jack that we are ready to do our thing */
	jack_activate(client);
	
	/* wait until this app receives a SIGINT (i.e. press 
	 * ctrl-c in the terminal) see signalled() above */
	while (!quit) 
	{
	// operate midi
		 /* let's not waste cycles by busy waiting */
		sleep(1);
	
	}
	//printf("bla\n"); fflush(stdout);
	
	/* so we shall quit, eh? ok, cleanup time. otherwise 
	 * jack would probably produce an xrun
	 * on shutdown */
	jack_deactivate(client);

	/* shutdown cont. */
	jack_client_close(client);

	/* done !! */
	return 0;
}

void error(int num, const char *msg, const char *path)
{
    printf("liblo server error %d in path %s: %s\n", num, path, msg);
    fflush(stdout);
}

/* catch any incoming messages and display them. returning 1 means that the
 * message has not been fully handled and the server should try other methods */
int generic_handler(const char *path, const char *types, lo_arg **argv,
		    int argc, void *data, void *user_data)
{
  if ( (argv[0]->i < _MULTITEMP) && (argv[1]->i < _CHOICEMAX) )
  		{
  			
    	choice[argv[0]->i][argv[1]->i]=argv[2]->i;
    	return 0;
    	}
    	else return 1;
    
}


int foo_handler(const char *path, const char *types, lo_arg **argv, int argc,
		 void *data, void *user_data)
{
    /* example showing pulling the argument values out of the argv array */
   int voice =  argv[0]->i;
   int i =  argv[1]->i;
   if ((voice<_MULTITEMP)&&(i>0) && (i<_PARACOUNT)) parameter[voice][i]=argv[2]->f;
   //if ((i==10) && (parameter[10]!=0)) parameter[10]=1000.f;
   // printf("%s <- f:%f, i:%d\n\n", path, argv[0]->f, argv[1]->i);
   // fflush(stdout);
   switch (i)
   {
   	 // reset the filters 
   	 case 0:{
   	 	low[voice][0]	= 0.f;
   	 	high[voice][0]	= 0.f;
   	 	band[voice][0] = 0.f;
   	 	low[voice][1]	= 0.f;
   	 	high[voice][1]	= 0.f;
   	 	band[voice][1] = 0.f;
   	 	low[voice][2]	= 0.f;
   	 	high[voice][2]	= 0.f;
   	 	band[voice][2] = 0.f;
   	 	phase[voice][1] = 0.f;
   	 	phase[voice][2] = 0.f;
   	 	phase[voice][3] = 0.f;
   	 break;}
   	 
   	 case 60:EG[voice][1][1]=argv[2]->f;break;
   	 case 61:EG[voice][1][2]=argv[2]->f;break;
   	 case 62:EG[voice][1][3]=argv[2]->f;break;
   	 case 63:EG[voice][1][4]=argv[2]->f;break;
   	 case 64:
   	 {
   	 	EGrepeat[voice][1] = (argv[2]->f>0) ? 1:0;
   	 	if (EGrepeat[voice][1] > 0 ) egStart(voice,1);
   	 	break;
   	 }
   	 case 65:EG[voice][2][1]=argv[2]->f;break;
   	 case 66:EG[voice][2][2]=argv[2]->f;break;
   	 case 67:EG[voice][2][3]=argv[2]->f;break;
   	 case 68:EG[voice][2][4]=argv[2]->f;break;
   	  case 69:
   	 {
   	 	EGrepeat[voice][2] = (argv[2]->f>0) ? 1:0;
   	 	if (EGrepeat[voice][2] > 0 ) egStart(voice,2);
   	 	break;
   	 }
   	 case 70:EG[voice][3][1]=argv[2]->f;break;
   	 case 71:EG[voice][3][2]=argv[2]->f;break;
   	 case 72:EG[voice][3][3]=argv[2]->f;break;
   	 case 73:EG[voice][3][4]=argv[2]->f;break;
   	  case 74:
   	 {
   	 	EGrepeat[voice][3] = (argv[2]->f>0) ? 1:0;
   	 	if (EGrepeat[voice][3] > 0 ) egStart(voice,3);
   	 	break;
   	 }
   	 case 75:EG[voice][4][1]=argv[2]->f;break;
   	 case 76:EG[voice][4][2]=argv[2]->f;break;
   	 case 77:EG[voice][4][3]=argv[2]->f;break;
   	 case 78:EG[voice][4][4]=argv[2]->f;break; 
   	  case 79:
   	 {
   	 	EGrepeat[voice][4] = (argv[2]->f>0) ? 1:0;
   	 	if (EGrepeat[voice][4] > 0 ) egStart(voice,4);
   	 	break;
   	 }
   	 case 80:EG[voice][5][1]=argv[2]->f;break;
   	 case 81:EG[voice][5][2]=argv[2]->f;break;
   	 case 82:EG[voice][5][3]=argv[2]->f;break;
   	 case 83:EG[voice][5][4]=argv[2]->f;break;
   	  case 84:
   	 {
   	 	EGrepeat[voice][5] = (argv[2]->f>0) ? 1:0;
   	 	if (EGrepeat[voice][5] > 0 ) egStart(voice,5);
   	 	break;
   	 }
   	 case 85:EG[voice][6][1]=argv[2]->f;break;
   	 case 86:EG[voice][6][2]=argv[2]->f;break;
   	 case 87:EG[voice][6][3]=argv[2]->f;break;
   	 case 88:EG[voice][6][4]=argv[2]->f;break;
   	  case 89:
   	 {
   	 	EGrepeat[voice][6] = (argv[2]->f>0) ? 1:0;
   	 	if (EGrepeat[voice][6] > 0 ) egStart(voice,6);
   	 	break;
   	 }
   	 case 102:EG[voice][0][1]=argv[2]->f;break;
   	 case 103:EG[voice][0][2]=argv[2]->f;break;
   	 case 104:EG[voice][0][3]=argv[2]->f;break;
   	 case 105:EG[voice][0][4]=argv[2]->f;break;
   	 
   }
   //float g=parameter[30]*parameter[56]+parameter[33]*(1.0f-parameter[56]);
   printf("%i %i %f \n",voice,i,argv[2]->f);
    return 0;
}

int quit_handler(const char *path, const char *types, lo_arg **argv, int argc,
		 void *data, void *user_data)
{
    done = 1;
    printf("quiting\n\n");
    fflush(stdout);

    return 0;
}
