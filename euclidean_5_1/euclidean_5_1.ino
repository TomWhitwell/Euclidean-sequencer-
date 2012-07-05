#include <EEPROM.h>
#include <LedControl.h>
#define brightness 8
#define debug 0 // 0 = normal 1 =  (internal clock) 2= SerialDump
#define display_update 2000 // how long active channel display is shown 
int length=50; //pulse length

/*

 
 To do 
- Error checking for eeprom reads / values to reduce risk of crashes 
- Find cause of ocassional skipped beats? 
 - Implement preset values of N - so it starts with 16/8/4

 
 Done 
 - Connect 'off beat' for channel 1 to the spare output  
- something causing channel 1 to stick - N changes don't appear 
 - When N is turned down, reduce K accordingly 
- make tick pulse correctly 
  - Fix drawing of beats  as beats are playing 
 - binary display of K & N not right 
 - remove serial print / debug routine 
 - remove delay - replace with 'all pulses off after 5ms routine
 - Fix crashing issue with low n  
 - channels 1 and 3 require tweak to start running 
 
 UPDATE CONNECTIONS FOR REAL BOARD
 
 Display 
 din = d2
 clk = d3
 load = d4 
 
 Encoders 
 Encoder 1a - k / beats = d5
 Encoder 1b - k / beats = d6 
 Encoder 2a - n / length = d7 
 Encoder 2b - n / length  = d8 
 Encoder 3a - Offset = d9 
 Encoder 3 b - offset = d10 
 */

#define enc1a 10
#define enc1b 9
#define enc2a 7
#define enc2b 8
#define enc3a 5
#define enc3b 6
#define sparepin 17

/*
 
 
 Pulses 
 1 = d11
 2 = d12
 3 = d13
 
 Switches / misc 
 encoder switch = A0 
 reset switch = A1 
 pulse input = A2
 spare jack out = A3 
 
 
 
 15k resistor ladder around 3 push button switches 
 +5v -- 15k -- switch -- 15k -- switch -- 15k -- switch -- 15k -- GND 
 Other ends of switches going to Analog in 0 
 
 */



LedControl lc=LedControl(2,3,4,1);

unsigned long time;
unsigned long last_sync;

int clocks[] = {
  4,8,12,16,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}; // possible values for n - to make knob easier by offering presets
int noclocks = 19; // how many possible ns are available?
/*

 
 
 DONE
 Display OK 
 Pulse in working up to audio rates 
 Got flashing working - system is outputting euclidean beats! 
 Integrate encoders & eeprom 
 Add encoder display - light up row as it's turning DONE
 3-way switch working 
 Active channel indicator 
 Sleep / wake system & animation 
 
 
 TO DO:
 Improve encoder display - flashes too fast 
 Implement offset 
 Ad
 
 BUGS: 
 FIXED Still display issues - 1111 on the end of beat displays - seems not to be clearing properly 
 
 FIXED Display on beats where k=n - seems to show loads of binary 111 on the end
 FIXED  Fix the bottom row - currently output flashers reversed 
 
 
 
 */



int channels = 3;
unsigned int beat_holder[3];

/*
Eeprom schema: 
 Channel 1: n = 1 k = 2 o = 7
 Channel 2: n = 3 k = 4 o = 8
 Channel 3: n = 5 k = 6 0 = 9 
 */

unsigned int channelbeats[3][4]={
  {
    EEPROM.read(1),EEPROM.read(2),0,EEPROM.read(7)            }
  ,{
    EEPROM.read(3),EEPROM.read(4),0, EEPROM.read(8)           }
  ,{
    EEPROM.read(5),EEPROM.read(6),0,EEPROM.read(9)            }
}; // 0=n, 1=k,2 = position , 3 = offset




int a; 
int changes=0;
boolean sleep=true;
int masterclock=0;
int read_head;
unsigned int  looptracker;

int old_total;//for knobs
int old_pulses;//for knobs

int pulseinput=2;
int newpulse;//for trigger in
int oldpulse=1;//for trigger in

boolean diga_old; // for encoders 
boolean digb_old;
boolean pulses_active = false; // is active while a beat pulse is playing 
boolean lights_active = false;   

int kknob;
int active_channel =1; // which channel is active? zero indexed 
int nknob;
int oknob; 
int maxn = 16; // maximums and minimums for n and k 
int minn = 1;
int mink = 1; 
int nn; 
int kk; 
unsigned long last_read; 
unsigned long last_changed; 
#define read_delay  50 // for debouncing 
int channel_switch;
int reset_button; 
int channel_switch_read;



void setup() {

  /*
   The MAX72XX is in power-saving mode on startup,
   we have to do a wakeup call
   */
  lc.shutdown(0,true);
  /* Set the brightness to a medium values */
  lc.setIntensity(0,brightness);
  /* and clear the display */
  lc.clearDisplay(0);

  if (EEPROM.read(1) > 17){ // if eprom is blank / corrupted, write some startup amounts

    EEPROM.write(1,16);
    EEPROM.write(2,4);
    EEPROM.write(3,12);
    EEPROM.write(4,6);
    EEPROM.write(5,8);
    EEPROM.write(6,5);
  }

  digitalWrite(enc1a, HIGH);       // turn on pullup resistor
  digitalWrite(enc1b, HIGH);       // turn on pullup resistor
  digitalWrite(enc2a, HIGH);       // turn on pullup resistor
  digitalWrite(enc2b, HIGH);       // turn on pullup resistor
  digitalWrite(enc3a, HIGH);       // turn on pullupresistor
  digitalWrite(enc3b, HIGH);       // turn on pullup resistor
  if (debug == 2){
    Serial.begin(9600);
  }
  for (a=11;a<14;a++){
    pinMode (a,OUTPUT);
  }

  // DEFINE SPARE PIN AS OUTPUT PIN 
  pinMode (sparepin, OUTPUT);

  // initialise beat holders 

  for (int a=0;a<channels;a++){
    beat_holder[a] = euclid(channelbeats[a][0],channelbeats[a][1]);
  }

}

void loop() {
  /*
What's in the loop: 
   Update time variable 
   Check to see if it is time go go to sleep 
   Changes routine - update beat_holder when channelbeats changes - triggered by changes == true
   Trigger routines - on trigget update displays and pulse
   Read encoders 
   Read switches 
   
   */



  time=millis();

  // COPY OVER N & K VARIABLES FOR EASE OF CODE READING 
  nn = channelbeats[active_channel][0];  
  kk = channelbeats[active_channel][1]; 


  // DEBUG PULSE TRIGGER & print out  
  if (debug >0 && time-last_sync > 250){
    Sync();
    if (debug ==2){
      Serial.print ("nn=");
      Serial.print (nn);
      Serial.print (" kk=");
      Serial.println (kk); 
    }   
  };

  // SLEEP ROUTINE 
  if (sleep == false && time-last_sync>10000)
  {      
    sleepanim();
    lc.shutdown(0,true); 
    sleep = true; 
  }

  // UPDATE BEAT HOLDER WHEN KNOBS ARE MOVED

  if (changes > 0){  
    beat_holder[active_channel] = euclid(nn,kk);
    lc.setRow(0,active_channel*2+1,0);//clear active row
    lc.setRow(0,active_channel*2,0);//clear line above active row


    if (changes == 1){  // 1 = K changes - display beats in the active channel 
      for (a=0;a<8;a++){ 
        if (bitRead (beat_holder[active_channel],nn-1-a)==1 && a<nn){
          lc.setLed(0,active_channel*2,7-a,true);
        }  
        if (bitRead (beat_holder[active_channel],nn-1-a-8)==1 && a+8<nn){
          lc.setLed(0,active_channel*2+1,7-a,true);
        }  
      }
    }

    if (changes == 2){ // 2 = N changes, display total length of beat 
      for (a=0;a<8;a++){ 
        if (a<nn){
          lc.setLed(0,active_channel*2,7-a,true);
        }  
        if (a+8<nn){
          lc.setLed(0,active_channel*2+1,7-a,true);
        }  
      }
    }



    changes = 0;  
    last_changed = time; 
  }

  // ANALOG PULSE TRIGGER 
  newpulse=map(analogRead(pulseinput),0,1024,0,3); // Pulse input 
  if (newpulse>oldpulse){
    Sync();
  }
  oldpulse = newpulse;


  // READ K KNOB - 
  kknob = EncodeRead(enc1a,enc1b); 
  if (kknob != 0 && time-last_read>read_delay) { 

    if (channelbeats[active_channel][1]+kknob > channelbeats[active_channel][0]-1) {
      kknob=0;
    }; // check within limits
    if (channelbeats[active_channel][1]+kknob < mink) {
      kknob=0;
    };


// CHECK AGAIN FOR LOGIC
if (channelbeats[active_channel][1] > channelbeats[active_channel][0]-1){
  channelbeats[active_channel][1] = channelbeats[active_channel][0]-1;};


    channelbeats[active_channel][1] = channelbeats[active_channel][1]+kknob; // update with encoder reading
    EEPROM.write((active_channel*2)+2,channelbeats[active_channel][1]); // write settings to 2/4/6 eproms 
    last_read = millis();
    changes = 1; // K change = 1
  }

// READ N KNOB 
  nknob = EncodeRead(enc2a,enc2b); 
  if (nknob != 0 && time-last_read>read_delay) { 

    // Sense check n encoder reading to prevent crashes 

    if (nn+nknob > maxn) {
      nknob=0;   
    }; // check below maxn
    if (nn+nknob < minn) {
      nknob=0;   
    }; // check above minn
    if (kk > nn+nknob-1 && kk>1){// check if new n is lower than k + reduce K if it is 
//     nknob=0;   
      channelbeats[active_channel][1] = channelbeats[active_channel][1]+nknob;
      
    }; 

    channelbeats[active_channel][0] = nn+nknob; // update with encoder reading
   kk = channelbeats[active_channel][1];
    nn = channelbeats[active_channel][0];  // update nn for ease of coding 


    EEPROM.write((active_channel*2)+1,channelbeats[active_channel][0]); // write settings to 2/4/6 eproms 
    last_read = millis();
    changes = 2; // n change = 2 
  }


  // SELECT ACTIVE CHANNEL 
  channel_switch_read = analogRead(A0); 
  if (channel_switch_read<120){
    channel_switch = 3;
  };
  if (channel_switch_read>121 && channel_switch_read<240){
    channel_switch = 2;
  };
  if (channel_switch_read>241 && channel_switch_read<400){
    channel_switch = 1;
  };
  if (channel_switch_read>401){
    channel_switch = 0;
  };
  if (channel_switch !=3){
    active_channel = channel_switch;


    lc.setRow(0,6,false); //clear row 7 
    lc.setLed(0,6,5-(active_channel*2),true);
    lc.setLed(0,6,5-(active_channel*2)-1,true);
  }; 

  // ENABLE RESET BUTTON 
  reset_button = analogRead(A1);
  if (reset_button>500 && channelbeats[0][2]>0){
    for (a=0; a<channels; a++){
      channelbeats[a][2]=0; 

    }

  }

// TURN OFF ANY LIGHTS THAT ARE ON 
  if (time-last_sync>length && lights_active==true){
    for(a=0;a<channels;a++){  
      lc.setLed(0,7,5-(a*2),false);
              lc.setLed(0,7,4,false); // spare pin flash 

    }
      lights_active = false; 
  }


// FINISH ANY PULSES THAT ARE ACTIVE - PULSES LAST 1/4 AS LONG AS LIGHTS 
  if (time-last_sync>(length/4) && pulses_active==true){
    for(a=0;a<channels;a++){  
      digitalWrite(11+a,LOW);
        digitalWrite(sparepin, LOW);

    }
      pulses_active = false; 
  }



}




// Euclid calculation function 

unsigned int euclid(int n, int k){ // inputs: n=total, k=beats, o = offset
  int pauses = n-k;
  int pulses = k;
  int per_pulse = pauses/k;
  int remainder = pauses%pulses;  
  unsigned int workbeat[n];
  unsigned int outbeat;
  unsigned int working;
  int workbeat_count=n;
  int a; 
  int b; 
  int trim_count;
  for (a=0;a<n;a++){ // Populate workbeat with unsorted pulses and pauses 
    if (a<pulses){
      workbeat[a] = 1;
    }
    else {
      workbeat [a] = 0;
    }
  }

  if (per_pulse>0 && remainder <2){ // Handle easy cases where there is no or only one remainer  
    for (a=0;a<pulses;a++){
      for (b=workbeat_count-1; b>workbeat_count-per_pulse-1;b--){
        workbeat[a]  = ConcatBin (workbeat[a], workbeat[b]);
      }
      workbeat_count = workbeat_count-per_pulse;
    }

    outbeat = 0; // Concatenate workbeat into outbeat - according to workbeat_count 
    for (a=0;a < workbeat_count;a++){
      outbeat = ConcatBin(outbeat,workbeat[a]);
    }
    return outbeat;
  }

  else { 


    int groupa = pulses;
    int groupb = pauses; 
    int iteration=0;
    if (groupb<=1){
    }
    while(groupb>1){ //main recursive loop


      if (groupa>groupb){ // more Group A than Group B
        int a_remainder = groupa-groupb; // what will be left of groupa once groupB is interleaved 
        trim_count = 0;
        for (a=0; a<groupa-a_remainder;a++){ //count through the matching sets of A, ignoring remaindered
          workbeat[a]  = ConcatBin (workbeat[a], workbeat[workbeat_count-1-a]);
          trim_count++;
        }
        workbeat_count = workbeat_count-trim_count;

        groupa=groupb;
        groupb=a_remainder;
      }


      else if (groupb>groupa){ // More Group B than Group A
        int b_remainder = groupb-groupa; // what will be left of group once group A is interleaved 
        trim_count=0;
        for (a = workbeat_count-1;a>=groupa+b_remainder;a--){ //count from right back through the Bs
          workbeat[workbeat_count-a-1] = ConcatBin (workbeat[workbeat_count-a-1], workbeat[a]);

          trim_count++;
        }
        workbeat_count = workbeat_count-trim_count;
        groupb=b_remainder;
      }




      else if (groupa == groupb){ // groupa = groupb 
        trim_count=0;
        for (a=0;a<groupa;a++){
          workbeat[a] = ConcatBin (workbeat[a],workbeat[workbeat_count-1-a]);
          trim_count++;
        }
        workbeat_count = workbeat_count-trim_count;
        groupb=0;
      }

      else {
        //        Serial.println("ERROR");
      }
      iteration++;
    }


    outbeat = 0; // Concatenate workbeat into outbeat - according to workbeat_count 
    for (a=0;a < workbeat_count;a++){
      outbeat = ConcatBin(outbeat,workbeat[a]);
    }




    return outbeat;

  }
}



// Function to find the binary length of a number by counting bitwise 
int findlength(unsigned int bnry){
  boolean lengthfound = false;
  int length=1; // no number can have a length of zero - single 0 has a length of one, but no 1s for the sytem to count
  for (int q=32;q>=0;q--){
    int r=bitRead(bnry,q);
    if(r==1 && lengthfound == false){
      length=q+1;
      lengthfound = true;
    }
  }
  return length;
}

// Function to concatenate two binary numbers bitwise 
unsigned int ConcatBin(unsigned int bina, unsigned int binb){
  int binb_len=findlength(binb);
  unsigned int sum=(bina<<binb_len);
  sum = sum | binb;
  return sum;
}


// routine triggered by each beat
void Sync(){ 
  if (sleep == true)// wake up routine & animation 
  {  
    lc.shutdown(0,false); 
    sleep = false;
    wakeanim();
  }


  // clear bottom row 
  //lc.setRow(0,7,0);

  if(masterclock%2==0){ // tick bottom left corner on and off with clock 
    lc.setLed(0,7,7,true);
  }
  else{
    lc.setLed(0,7,7,false);
  }


  // Cycle through channels 
  for(a=0;a<channels;a++){

    read_head = channelbeats[a][0]-channelbeats[a][2]-1;  
  if (a != active_channel  || time-last_changed>display_update) // don't clear or draw cursor if channel is being changed 
{


    lc.setRow(0,a*2,0);//clear line above active row
    
    if (channelbeats[a][2]<8){
    
    for (int c=0;c<8;c++){ 
      if (bitRead (beat_holder[a],channelbeats[a][0]-1-c)==1 && c<channelbeats[a][0]){
        lc.setLed(0,a*2,7-c,true);
      }  
    }
    }
   else {
        for (int c=8;c<16;c++){  
     
      if (bitRead (beat_holder[a],channelbeats[a][0]-1-c)==1 && c<channelbeats[a][0]){
            lc.setLed(0,a*2,15-c,true);
           } 
    }
   }


    lc.setRow(0,a*2+1,0);//clear active row
    // draw cursor 
    if (channelbeats[a][2]<8){  
      lc.setLed(0,a*2+1,7-channelbeats[a][2],true); // write cursor less than 8
    }
    else if(channelbeats[a][2]>=8 && channelbeats[a][2]<16){
      lc.setLed(0,a*2+1,15-channelbeats[a][2],true); // write cursor more than 8
    }  
  
}
  // turn on pulses on channels where a beat is present  
  if (bitRead (beat_holder[a],read_head)==1){
    digitalWrite(11+a,HIGH); // pulse out 
    lc.setLed(0,7,5-(a*2),true); // bottom row flash 
    pulses_active = true;   
        lights_active = true;   
  }

  // send off pulses to spare output for the first channel 
  if (bitRead (beat_holder[a],read_head)==0 && a == 0){ // only relates to first channel 
    digitalWrite(sparepin,HIGH); // pulse out 
    lc.setLed(0,7,4,true); // bottom row flash 
    pulses_active = true;   
        lights_active = true;   
  }

  

  // move counter to next position, ready for next pulse  
  channelbeats[a][2]++;
  if (channelbeats[a][2]>=channelbeats[a][0]){
    channelbeats[a][2] = 0; 
  }
  }



  masterclock++;
  if (masterclock>=16){
    masterclock=0;
  };

  looptracker++;

length = ((time-last_sync)/5);
  last_sync = time;

}

/* function to read encoders at the designated pins 
 returns +1, 0 or -1 dependent on direction 
 Contains no internal debounce, so calls should be delayed 
 */

int EncodeRead(int apin, int bpin){ 

  boolean diga=digitalRead(apin);
  boolean digb=digitalRead(bpin);
  int result=0;
  if (diga == diga_old && digb == digb_old){
    result=0;
  }

  else if (diga == true && digb == false){ 
    result=1;
    diga_old=diga;
    digb_old=digb;
  }  

  else if (diga == false && digb == true){ 
    result=-1;
    diga_old=diga;
    digb_old=digb;
  }  
  else if (diga == false && digb == false && diga_old == true && digb_old == false){
    result=1;
    diga_old=diga;
    digb_old=digb;
  }

  else if (diga == false && digb == false && diga_old == false && digb_old == true){
    result=-1;
    diga_old=diga;
    digb_old=digb;
  }

  else if (diga == false && digb == false){ 
    result=0;
    diga_old=diga;
    digb_old=digb;
  }   
  return result;
}

void wakeanim(){
  for (a=4; a>0;a--){
    lc.setIntensity(0,8-a*2);  
    lc.setRow(0,a,255); 
    lc.setRow(0,8-a,255); 
    delay(100);
    lc.setRow(0,a,0);
    lc.setRow(0,8-a,0); 
  }
}
void sleepanim(){
  for (a=0; a<4;a++){
    lc.setIntensity(0,a*2);  
    lc.setRow(0,a,255); 
    lc.setRow(0,8-a,255); 
    delay(200);
    lc.setRow(0,a,0);
    lc.setRow(0,8-a,0); 
  }

}  





