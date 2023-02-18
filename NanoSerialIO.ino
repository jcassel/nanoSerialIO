//simple D1 mini Serial IO Driver
//Takes serial commands to control configured IO.
//Can create automations for interactions between inputs, outputs and actions. (Future)

//Eventually should support a closed loop temerature control. Need to have a sensor and a output to control a heating element. Could also do a fan / exaust system to cool enclosure down. 

//Target outputs 
//Digital/relay: PSU,Lighting,Enclosure heater, alarm, filter relay,exaust fan
//Digital (PWM):cooling fan(PWM*) (Future)

//Target inputs
//Digital(5v/3.3v): General switches(NO or NC Selectable)
//temperature inputs(Future + TBA on type)


#define VERSIONINFO "NanoSerialIO 1.0.3"
#define COMPATIBILITY "SIOPlugin 0.1.1"
#include "TimeRelease.h"
#include <Bounce2.h>
#include <EEPROM.h>

#define IO0 2   // pin2/D0  //cant be used for this... used as TX on the comm port
#define IO1 1   // pin1/D1  //cant be used for this... used as RX on the comm port
#define IO2 2   // pin5/D5
#define IO3 3   // pin6/D3
#define IO4 4   // pin7/D4
#define IO5 5   // pin8/D5
#define IO6 6   // pin9/D6
#define IO7 7  // pin10/D7
#define IO8 8  // pin11/D8
#define IO9 9  // pin12/D9
#define IO10 10  // pin13/D10
#define IO11 11  // pin14/D11
#define IO12 12  // pin15/D12
#define IO13 13 // pin16/D13(LED)*

#define IO14 A0 // pin19/A0  //Note that I had some experences with these not behaving when configured as inputs without a load of some type.Reporting 0 even when nothing pulling to ground as a (INPUT_PULLUP). 
#define IO15 A1 // pin20/A1
#define IO16 A2 // pin21/A2
#define IO17 A3 // pin22/A3
#define IO18 A4 // pin23/A4
#define IO19 A5 // pin24/A5
//Note that A6 and A7 can only be used as Analog inputs. 
//Arduino Documentation also warns that Any input that it floating is suseptable to random readings. Best way to solve this is to use a pull up(avalible as an internal) or down resister(external).


#define IOSize  20 //(0-19)note that this equates to 18 bacause of D0andD1 as Serial.
#define NU 9  //this is used to skip configuration of IO points we might not be able to use but want to leave in the mapping for pin maping consistance and such.
bool _debug = false;
int IOType[IOSize]{NU,NU,INPUT_PULLUP,INPUT_PULLUP,INPUT_PULLUP,INPUT_PULLUP,INPUT_PULLUP,INPUT_PULLUP,INPUT_PULLUP,INPUT_PULLUP,INPUT_PULLUP,INPUT_PULLUP,INPUT_PULLUP,INPUT_PULLUP,OUTPUT,OUTPUT,OUTPUT,OUTPUT,OUTPUT,OUTPUT}; //0-19
int IOMap[IOSize] {IO0,IO1,IO2,IO3,IO4,IO5,IO6,IO7,IO8,IO9,IO10,IO11,IO12,IO13,IO14,IO15,IO16,IO17,IO18,IO19};
int IO[IOSize];
Bounce Bnc[IOSize];
bool EventTriggeringEnabled = 1;

/*
Serial Commands supported

If a command is recognized it will return OK. 
Then execute the command. Any results of that command will come after.

Command: IO [a]:[b] is used to set an IO point on or off by index.
where [a] is the IO index and [b] is 1(HIGH) or 0(LOW)

Command: SI [n] is used to adjust the auto reporting timing in milliseconds.(500 min)
[n] is a range from 500 to 30000.

Command: EIO will pause Autoreporting if it is enabled.

Command: BIO will resume Autoreporting if it is enabled. Does not set Autoreporting true.

Command: debug [n] turns on or off serial debug messaging.
[n] is 1 for on and 0 for off.

Command: IOT will return the current IO Configuration.

Command: SE [n] will enable or disable event triggering.
If enabled when an input is triggered, an IO report will be imediatly triggered.
This is good for EStops and getting faster responses to input changes
[n] is 1 for on and 0 for off.

Command: GS will trigger an IO Report.

*/

int trustVal = 73; //specifics of number is completely random. 

void StoreIOConfig(){
  Serial.println("Storing IO Config.");
  //EEPROM.update(0,trustVal); //Just Writing a well known predictable value in first eprom position to indicate that IO types were stored at least once. Allows us to trust the read. Not fool proof but pretty good trust level.
  for (int i=0;i<IOSize;i++){
    //EEPROM.update(i+1,IOType[i]); //store IO type map in eeprom but only if they are different... Will make it last a little longer but unlikely to really matter.:) 
    if(_debug){Serial.print("Writing to EE pos:");Serial.print(i);Serial.print(" type:");Serial.println(IOType[i]);}
  }
}


void FetchIOConfig(){
  int testVal = EEPROM.read(0); //specifics of number is completely random. 
  if(testVal == trustVal){ //if false,we can't trust what is in the eeprom. Likely did not save a config yet.
    for (int i=2;i<IOSize;i++){ //starting at 2 to avoide reading in first 2 IO points.
      IOType[i] = EEPROM.read(i+1);//retreve IO type map from eeprom. 
    }
    return;
  }
  Serial.print("ALERT: Can't trust stored IO config. Using defaults. ");Serial.println(testVal);
}



bool isOutPut(int IOP){
  return IOType[IOP] == OUTPUT; 
}

void ConfigIO(){
  
  Serial.println("Setting IO");
  for (int i=0;i<IOSize;i++){
    if(IOType[i] != NU){ //skip config of any that are outside scope for Digital IO
      if(IOType[i] == 0 ||IOType[i] == 2 || IOType[i] == 3){ //if it is an input
        Bnc[i].attach(IOMap[i],IOType[i]);
        Bnc[i].interval(5);
      }else{
        pinMode(IOMap[i],IOType[i]);
      }
    }
  }

}



TimeRelease IOReport;
TimeRelease IOTimer[9];
int reportInterval = 3000;



void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(300);
  Serial.println(VERSIONINFO);
  Serial.println("Start Types");
  Serial.print("LED on PIO:");
  Serial.println(LED_BUILTIN); 
  FetchIOConfig();
  ConfigIO();
  reportIOTypes();
  Serial.println("End Types");

  //need to get a baseline state for the inputs and outputs.
  for (int i=0;i<IOSize;i++){
    IO[i] = digitalRead(IOMap[i]);  
  }
  
  IOReport.set(100ul);
  
}


bool _pauseReporting = false;
void loop() {
  // put your main code here, to run repeatedly:
  checkSerial();
  if(!_pauseReporting){
    reportIO(checkInputs());
  }
  
}


void reportIO(bool forceReport){
  if (IOReport.check()||forceReport){
    Serial.print("IO:");
    for (int i=0;i<IOSize;i++){
      if(IOType[i] == 1 ){ //if it is an output
        IO[i] = digitalRead(IOMap[i]);  
      }
      Serial.print(IO[i]);
    }
    Serial.println();
    IOReport.set(reportInterval);
    
  }
}

bool checkInputs(){
  bool changed = false;
  for (int i=0;i<IOSize;i++){
    Bnc[i].update();
    if(Bnc[i].changed()){
     changed = true;
     IO[i]=Bnc[i].read();
    }
  }
  return changed;
}


void reportIOTypes(){
  for (int i=0;i<IOSize;i++){
    Serial.print(IOType[i]);
    //if(i<IOSize-1){Serial.print(";");}
  }
  Serial.println();
}

void checkSerial(){
  if (Serial.available()){
    
    String buf = Serial.readString();
    buf.trim();
    if(_debug){Serial.print("buf:");Serial.println(buf);}
    int sepPos = buf.indexOf(" ");
    String command ="";
    String value = "";
    
    if(sepPos){
      command = buf.substring(0,sepPos);
      value = buf.substring(sepPos+1);;
      if(_debug){
        Serial.print("command:");Serial.print("[");Serial.print(command);Serial.println("]");
        Serial.print("value:");Serial.print("[");Serial.print(value);Serial.println("]");
      }
    }else{
      command = buf;
      if(_debug){
        Serial.print("command:");Serial.print("[");Serial.print(command);Serial.println("]");
      }
    }
    
    if(command == "BIO"){ 
      ack();
      _pauseReporting = false; //restarts IO reporting 
      
    }    
    else if(command == "EIO"){ //this is the command meant to test for good connection.
      ack();
      _pauseReporting = true; //stops all IO reporting so it does not get in the way of a good confimable response.
      //Serial.print("Version ");
      //Serial.println(VERSIONINFO);
      //Serial.print("COMPATIBILITY ");
      //Serial.println(COMPATIBILITY);
      
    }
    else if (command == "IC") { //io count.
      ack();
      Serial.print("IC:");
      Serial.println(IOSize);
    }
    
    else if(command =="debug"){
      ack();
      if(value == "1"){
        _debug = true;
        Serial.println("Serial debug On");
      }else{
        _debug=false;
        Serial.println("Serial debug Off");
      }
    }
    else if(command=="CIO"){ //set IO Configuration
      ack();
      if (validateNewIOConfig(value)){
        updateIOConfig(value);
      }
    }
    
    else if(command=="SIO"){
      ack();
      StoreIOConfig();
    }
    else if(command=="IOT"){
      ack();
      reportIOTypes();
    }
    
    //Set IO point high or low (only applies to IO set to output)
    else if(command =="IO" && value.length() > 0){
      ack();
      int IOPoint = value.substring(0,value.indexOf(" ")).toInt();
      int IOSet = value.substring(value.indexOf(" ")+1).toInt();
      if(_debug){
        Serial.print("IO #:");
        Serial.println(IOMap[IOPoint]);
        Serial.print("Set:");Serial.println(IOSet);
      }
      if(isOutPut(IOPoint)){
        if(IOSet == 1){
          digitalWrite(IOMap[IOPoint],HIGH);
        }else{
          digitalWrite(IOMap[IOPoint],LOW);
        }
      }else{
        Serial.println("ERROR: Attempt to set IO which is not an output");   
      }
        reportIO(true);
    }
    
    //Set AutoReporting Interval  
    else if(command =="SI" && value.length() > 0){
      ack();
      unsigned long newTime = value.toInt(); //will convert to a full long.
      if(newTime >=500){
        reportInterval = newTime;
      }else{
        Serial.println("ERROR: Value to small min 500ms");
      }
    }

    //Enable event trigger reporting Mostly used for E-Stop
    else if(command == "SE" && value.length() > 0){
      ack();
      EventTriggeringEnabled = value.toInt();
    }

    //Get States 
    else if(command == "GS"){
      ack();
      reportIO(true);
    }
    else{
      Serial.print("ERROR: Unrecognized command[");
      Serial.print(command);
      Serial.println("]");
    }
  }
}

void ack(){
  Serial.println("OK");
}

bool validateNewIOConfig(String ioConfig){
  
  if(ioConfig.length() != IOSize){
    return false;  
  }

  for (int i=0;i<IOSize;i++){
    int pointType = ioConfig.substring(i,i+1).toInt();
    if(pointType > 4){//cant be negative. we would have a bad parse on the number
      if(_debug){
        Serial.print("Bad IO Point type: index[");Serial.print(i);Serial.print("] type[");Serial.print(pointType);Serial.println("]");
      }
      return false;
    }
  }
  return true; //seems its a good set of point Types.
}

void updateIOConfig(String newIOConfig){
  for (int i=2;i<IOSize;i++){//start at 2 to avoid D0 and D1
    int nIOC = newIOConfig.substring(i,i+1).toInt();
    if(IOType[i] != nIOC){
      IOType[i] = nIOC;
      if(nIOC == OUTPUT){
        digitalWrite(IOMap[i],LOW); //set outputs to low since they will be high coming from INPUT_PULLUP
      }
    }
  }
}

int getIOType(String typeName){
  if(typeName == "INPUT"){return 0;}
  if(typeName == "OUTPUT"){return 1;}
  if(typeName == "INPUT_PULLUP"){return 2;}
  if(typeName == "INPUT_PULLDOWN"){return 3;}
  if(typeName == "OUTPUT_OPEN_DRAIN"){return 4;} //not sure on this value have to double check
}
