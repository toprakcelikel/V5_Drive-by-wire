#include <Arduino.h>
#include "DBW_Pins.h"
#include "Vehicle.h"
#include "Can_Protocol.h"
#include "Settings.h"
#include "FirmwareMode.h"
#if DBW_SERIAL_TEST_MODE
#include "SerialTestMode.h"
#include <string.h>

namespace {
const uint32_t MAX_STEER_MS = 1000;
const uint32_t OUTPUT_TIMEOUT_MS = 5000;
const uint8_t testRcPins[6] = {STEERING_CH1_PIN, THROTTLE_BR_CH2_PIN, CH3_PIN, CH4_PIN, CH5_PIN, CH6_PIN};
volatile uint32_t rcRise[6] = {}, rcWidth[6] = {}, rcSeen[6] = {};
volatile bool rcHigh[6] = {};

template <uint8_t channel> void testRcEdge() {
  uint32_t now = micros();
  if (digitalRead(testRcPins[channel])) {
    rcRise[channel] = now;
    rcHigh[channel] = true;
  } else if (rcHigh[channel]) {
    rcWidth[channel] = now - rcRise[channel];
    rcSeen[channel] = now;
    rcHigh[channel] = false;
  }
}

bool validTestValue(char command, long value) {
  switch (command) {
    case 'V': return value >= 5 && value <= 255;
    case 'S': return value >= -(long)MAX_STEER_MS && value <= (long)MAX_STEER_MS;
    case 'T': return value >= 0 && value <= 255;
    case 'P': return value >= MIN_LEFT_US && value <= MAX_RIGHT_US;
    default: return false;
  }
}
}
using SerialTestMode::report;
#endif

RC_Controller* Vehicle::RC;
SpeedController* Vehicle::throttle;
SteeringController* Vehicle::steer;

int16_t Vehicle::desired_speed_cmPs;
int16_t Vehicle::desired_brake;
int16_t Vehicle::desired_angle_DegX10; 

/****************************************************************************
   Constructor
 ****************************************************************************/
Vehicle::Vehicle(Startup startup) : startupMode(startup)
{
 
  // Intialize default values
  currentSpeed_cmPs = 0;
  currentAngle_DegX10 = 0;
  currentBrake = 0;
  desired_speed_cmPs = 0;
  desired_brake = 0;
  desired_angle_DegX10 = 0;
  currentDriveMode = FORWARD_MODE;
  currentAutoMode = INITIALIZING;
  canActive = false;
  last_nav_speed_cmPs = 0;
  last_nav_brake = 0;
  last_nav_mode = 0;
  last_nav_angle_DegX10 = 0;
  last_nav_status = 0;
  measured_wheel_angle_DegX10 = 0;

  if (startupMode == Startup::Diagnostic) {
    FirstTime = false;
#if DBW_SERIAL_TEST_MODE
    SerialTestMode::begin();
    analogWriteResolution(8);
    analogReadResolution(10);
    steer = new SteeringController(SteeringController::Startup::Diagnostic);
    throttle = new SpeedController(SpeedController::Startup::Diagnostic);
    beginTests();
#endif
    return;
  }

  RC = new RC_Controller();
  throttle = new SpeedController();
  steer = new SteeringController();

  // Can0.begin() returns the baud rate it achieved, so non-zero means success.
  // The two messages used to be swapped, reporting "failed" on a working bus.
  if (Can0.begin(CAN_BPS_500K))  // initialize CAN with 500kbps baud rate
  {
    Serial.println("Can0 init success");
  } else {
     Serial.println("Can0 init failed");
  }
  // Catch-all RX filter. Specific-ID watchFor() filters silently drop frames
  // on this Due; confirmed by DBW_CAN_RX_Test that catch-all receives 0x350
  // and 0x100 correctly. Single call leaves TX mailboxes free for sendCan().
  Can0.watchFor();
  FirstTime = true;
  Serial.println("Vehicle constructor finished.");
}

/*****************************************************************************
   Destructor
 ****************************************************************************/
Vehicle::~Vehicle() {
}

void Vehicle::test() {
#if DBW_SERIAL_TEST_MODE
  if (startupMode != Startup::Diagnostic) return;
  serviceTestOutputs();
  SerialTestMode::readCommands(*this);
  serviceTestOutputs();
  SerialTestMode::flushReports();
#endif
}

#if DBW_SERIAL_TEST_MODE
void Vehicle::beginTests() {
  pinMode(OP_ESTOP, INPUT_PULLUP);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(BLUE_LED_PIN, OUTPUT);
  for (uint8_t channel = 0; channel < 6; ++channel) {
    pinMode(testRcPins[channel], INPUT);
    rcRise[channel] = rcWidth[channel] = rcSeen[channel] = 0;
    rcHigh[channel] = false;
  }
  attachInterrupt(digitalPinToInterrupt(testRcPins[0]), testRcEdge<0>, CHANGE);
  attachInterrupt(digitalPinToInterrupt(testRcPins[1]), testRcEdge<1>, CHANGE);
  attachInterrupt(digitalPinToInterrupt(testRcPins[2]), testRcEdge<2>, CHANGE);
  attachInterrupt(digitalPinToInterrupt(testRcPins[3]), testRcEdge<3>, CHANGE);
  attachInterrupt(digitalPinToInterrupt(testRcPins[4]), testRcEdge<4>, CHANGE);
  attachInterrupt(digitalPinToInterrupt(testRcPins[5]), testRcEdge<5>, CHANGE);
  emergencyTestStop();
  report("DBW SERIAL TEST MODE: Programming USB, 115200, newline or CRLF");
  report("L colors | B brakes | R release | V 5..255 PWM | S signed ms | T 0..255 DAC");
  report("E immediate stop | C RC inputs | P 1000..1850 us servo; physical stop LOW inhibits commands");
  report("BENCH ONLY until pin polarity verified. DAC0=0 is NOT zero volts. No actuators attached.");
}

void Vehicle::setTestColor(uint8_t color) {
  digitalWrite(RED_LED_PIN, (color & 1) ? HIGH : LOW);
  digitalWrite(GREEN_LED_PIN, (color & 2) ? HIGH : LOW);
  digitalWrite(BLUE_LED_PIN, (color & 4) ? HIGH : LOW);
}

void Vehicle::stopTestMotion() {
  steer->stopTest();
  throttle->testThrottle(0);
  diagnostic.steeringActive = false;
  diagnostic.throttleActive = false;
}

void Vehicle::applyTestBrakes() {
  bool alreadyApplied = throttle->brakesApplied();
  throttle->Stop();
  diagnostic.throttleActive = false;
  if (!alreadyApplied) {
    report("BRAKE ON: D%u=%u D%u=%u; 800 ms boost", BRAKE_ON_PIN, ON_BR, BRAKE_VOLT_PIN, ON_BR);
  }
}

void Vehicle::emergencyTestStop() {
  stopTestMotion();
  applyTestBrakes();
  diagnostic.estopped = true;
  diagnostic.mode = 0;
  diagnostic.ledActive = false;
  diagnostic.rcActive = false;
  setTestColor(1);
  report("ESTOP: PWM=0 DAC0=0; brakes applied; numeric commands blocked");
}

void Vehicle::applyTestValue(char command, long value) {
  if (command == 'V') {
    diagnostic.steeringPwm = (uint8_t)value;
    report("STEER PWM setting=%ld (output remains 0 until S)", value);
  } else if (command == 'S') {
    stopTestMotion();
    if (value != 0) {
      steer->testMotor(value < 0, diagnostic.steeringPwm);
      diagnostic.steerDuration = (uint32_t)(value < 0 ? -value : value);
      diagnostic.steerStarted = millis();
      diagnostic.steeringActive = true;
    }
    report("STEER ms=%ld PWM=%u; initial angle=%d", value, diagnostic.steeringPwm, steer->readLeftSensorRaw());
  } else if (command == 'T') {
    stopTestMotion();
    if (!throttle->testThrottle((uint8_t)value)) {
      report("ERR brakes applied; use R then T before nonzero throttle");
      return;
    }
    diagnostic.throttleActive = value > 0;
    diagnostic.outputStarted = millis();
    report("THROTTLE DAC0=%ld; nonzero output expires in 5000 ms", value);
  } else if (command == 'P') {
    stopTestMotion();
    if (!steer->testPulse((uint16_t)value)) {
      report("ERR pulse outside configured steering limits");
      return;
    }
    diagnostic.outputStarted = millis();
    report("SERVO D%u=%ld us; angle=%d; detach in 5000 ms", STEER_PULSE_PIN, value, steer->readLeftSensorRaw());
  }
}

void Vehicle::testCommand(char command, long value, bool hasValue) {
  if (startupMode != Startup::Diagnostic) return;
  if (command == 'E') {
    emergencyTestStop();
    return;
  }
  bool letter = command != 0;
  if (!letter) command = diagnostic.mode;
  if (!command || !strchr("LBRVSTCP", command)) {
    report("ERR expected L B R V S T E C P");
    return;
  }
  if (hasValue && !validTestValue(command, value)) {
    report("ERR range: V 5..255; S -1000..1000 ms; T 0..255; P 1000..1850 us");
    return;
  }
  if (diagnostic.physicalStop || (!letter && diagnostic.estopped)) {
    report("ERR ESTOP latched or physical stop grounded");
    return;
  }
  if (letter) {
    stopTestMotion();
    diagnostic.ledActive = false;
    diagnostic.rcActive = false;
    diagnostic.estopped = false;
    setTestColor(0);
    diagnostic.mode = strchr("VSTP", command) ? command : 0;
    report("COMMAND %c; previous motion cancelled; ESTOP latch cleared", command);
    if (command == 'B') applyTestBrakes();
    if (command == 'R') {
      throttle->ReleaseBrakes();
      report("BRAKE OFF: D%u=%u D%u=%u", BRAKE_ON_PIN, OFF_BR, BRAKE_VOLT_PIN, OFF_BR);
    }
    if (command == 'L') {
      diagnostic.ledColor = 0;
      diagnostic.ledStarted = millis();
      diagnostic.ledActive = true;
      setTestColor(0);
      report("LED RGB mask=0 (off)");
    }
    if (command == 'C') {
      diagnostic.rcActive = true;
      diagnostic.rcReported = millis() - 250;
    }
  }
  if (hasValue) applyTestValue(command, value);
}

void Vehicle::serviceTestOutputs() {
  uint32_t now = millis();
  bool pressed = digitalRead(OP_ESTOP) == LOW;
  if (pressed && !diagnostic.physicalStop) emergencyTestStop();
  diagnostic.physicalStop = pressed;
  bool wasBoosting = throttle->brakeBoostActive();
  throttle->serviceBrakes();
  if (wasBoosting && !throttle->brakeBoostActive()) {
    report("BRAKE HOLD: D%u=%u D%u=%u", BRAKE_ON_PIN, ON_BR, BRAKE_VOLT_PIN, OFF_BR);
  }
  if (diagnostic.steeringActive && now - diagnostic.steerStarted >= diagnostic.steerDuration) {
    stopTestMotion();
    report("STEER STOP; angle=%d (10-bit raw); calibrated=%d degX10",
           steer->readLeftSensorRaw(), steer->computeAngleLeft());
  }
  if ((diagnostic.throttleActive || steer->pulseTestActive()) && now - diagnostic.outputStarted >= OUTPUT_TIMEOUT_MS) {
    emergencyTestStop();
    report("OUTPUT TIMEOUT; angle=%d", steer->readLeftSensorRaw());
  }
  if (diagnostic.ledActive && now - diagnostic.ledStarted >= 1000) {
    diagnostic.ledStarted = now;
    if (++diagnostic.ledColor == 8) {
      diagnostic.ledActive = false;
      setTestColor(0);
      report("LED cycle complete");
    } else {
      setTestColor(diagnostic.ledColor);
      report("LED RGB mask=%u (R=1 G=2 B=4)", diagnostic.ledColor);
    }
  }
  if (diagnostic.rcActive && now - diagnostic.rcReported >= 250) {
    diagnostic.rcReported = now;
    uint32_t widths[6], seen[6];
    noInterrupts();
    for (uint8_t channel = 0; channel < 6; ++channel) {
      widths[channel] = rcWidth[channel];
      seen[channel] = rcSeen[channel];
    }
    interrupts();
    uint32_t nowUs = micros();
    for (uint8_t channel = 0; channel < 6; ++channel) {
      if (nowUs - seen[channel] > 100000) widths[channel] = 0;
    }
    report("RC us: %lu %lu %lu %lu %lu %lu (0=missing/stale)",
           (unsigned long)widths[0], (unsigned long)widths[1], (unsigned long)widths[2],
           (unsigned long)widths[3], (unsigned long)widths[4], (unsigned long)widths[5]);
  }
}
#endif

/*******************************************************************************************************
   Called after updated desired settings are received from RC and CAN.
   Change Vehicle speed and steering angle settings
 *******************************************************************************************************/
void Vehicle::update() {
  if (startupMode == Startup::Diagnostic) {
    test();
    return;
  }
  // ____First, get desired values___________________________________________________
  // currentAutoMode is set by updateRC() before update() is called.
  // Do not re-derive it here; int2Auto() expects switch positions, not AutoMode enum values.
  int16_t tempDspeed;
  if (currentAutoMode == MANUAL_MODE && !canActive)
  {
    tempDspeed = RC->getMappedValue(CH2);
    if (tempDspeed == -1)
    {
      desired_brake = 100;  // brake on
      desired_speed_cmPs = 0;  // stop
    }
    else
    {
      desired_brake = 0;  // brake off
      desired_speed_cmPs = tempDspeed;
    }
    desired_angle_DegX10 =  RC->getMappedValue(CH1);
   }
  if (currentAutoMode == OPERATOR_MODE)
  {
    tempDspeed = RC->getMappedValue(CH2);
    if (tempDspeed == -1)
    {
      desired_brake = 100;  // brake on
      desired_speed_cmPs = 0;  // stop
    }
    else
    {
      desired_brake = 0;  // brake off
      desired_speed_cmPs = tempDspeed;
    }
    desired_angle_DegX10 = RC->getMappedValue(CH1);
  }
   if (currentAutoMode == AUTO_RC)
   {
    tempDspeed = RC->getMappedValue(CH2);
    if (tempDspeed == -1)
    {  // E-Stop!
      desired_brake = 100;  // brake on
      desired_speed_cmPs = 0;  // stop
      currentAutoMode  = ESTOP_RC;
    }
   //recieveCan();  //check for new message  
   }
  //____________-Check for actuator test on initialization_____________________________
  if (FirstTime)
  {
    FirstTime = false;
    if (currentDriveMode == REVERSE_MODE && currentAutoMode == MANUAL_MODE)
    {
      // Set status LED to cyan to show testing
      digitalWrite(RED_LED_PIN,LOW);  // Blue and green are already high
      steer->test();
      throttle->test();
       // Set status LED to white to show initializing
      digitalWrite(RED_LED_PIN,HIGH);
    }
  }
  //_____________Implement desired values__________________________________________________

  // Brake-over-throttle safety: an explicit brake command must dominate any
  // throttle request. Without this, desired_brake is parsed but never reaches
  // the throttle path, which only brakes when desired_speed_cmPs <= 0 — so a
  // "speed > 0 AND brake on" command would keep driving. Force the stop path.
  if (desired_brake > 0) desired_speed_cmPs = 0;

  currentSpeed_cmPs = throttle->update(desired_speed_cmPs, currentDriveMode);
  currentAngle_DegX10 = steer->update(desired_angle_DegX10, measured_wheel_angle_DegX10);
  outgoing.id = Actual_CANID;
  outgoing.length = 6;
  outgoing.data.int16[0] = currentSpeed_cmPs;
  outgoing.data.int16[2] = currentAngle_DegX10;
  sendCan();   // send current velocity

  // 0x200 LowStatus - status-bit mirror so Nav can see what mode DBW is in
  outgoing.id = LowStatus_CANID;
  outgoing.length = 1;
  uint8_t status = 0;
  if (currentAutoMode == ESTOP_RC || currentAutoMode == ESTOP_OP || currentAutoMode == ESTOP_BTN) status |= 0x80;
  if (currentAutoMode == AUTO_RC || currentAutoMode == AUTO_OP) status |= 0x40;
  if (currentDriveMode == REVERSE_MODE) status |= 0x04;
  outgoing.data.uint8[0] = status;
  sendCan();
}
//*************************************************************************************
void Vehicle::updateRC() {
  if (startupMode == Startup::Diagnostic) return;
  currentAutoMode = RC->updateMode(currentAutoMode);

  // Record what each source is asking for, regardless of which one is selected.
  // updateMode() has just refreshed both, and the "-1 means brake" convention
  // here matches update() below so the columns are directly comparable.
  long req = RC->getRCRequest(CH2);
  rc_brake        = (req == -1) ? 100 : 0;
  rc_speed_cmPs   = (req == -1) ? 0 : (int16_t)req;
  rc_angle_DegX10 = (int16_t)RC->getRCRequest(CH1);
  req = RC->getOPRequest(CH2);
  op_brake        = (req == -1) ? 100 : 0;
  op_speed_cmPs   = (req == -1) ? 0 : (int16_t)req;
  op_angle_DegX10 = (int16_t)RC->getOPRequest(CH1);

  if (currentAutoMode == ESTOP_RC || currentAutoMode == ESTOP_OP || currentAutoMode == ESTOP_BTN)
    throttle->Stop();
  currentDriveMode = RC->getDriveMode();
  for (int i = 0; i < RC_NUM_SIGNALS; i++)
  {
    RCtime[i] = RC->getEtime(i);  // Get the width of the pulse from RC: 1000 to 2000 us
    RCMapped[i] = RC->getMappedValue(i); // Convert pulse width
  }
}
//*************************************************************************************
AutoMode Vehicle::int2Auto( int amode)
{
  if (amode == 0) return MANUAL_MODE;
  if (amode == 1) return OPERATOR_MODE;
  if (amode == 2) return AUTO_RC;
  return  ESTOP_RC;
}
/************************************************************************************
*  Send current vehicle velocity over CAN
*************************************************************************************/
bool Vehicle::sendCan() {
  if (startupMode == Startup::Diagnostic) return false;
  if (Can0.sendFrame(outgoing))
    return (true);
  else 
    return (false);
}
/*************************************************************************************
   Checks for receipt of a message from CAN bus for new
   desired speed/angle/brake instructions from high-level board.
   Protocol per https://www.elcanoproject.org/wiki/Communication :
     0x350 HiDrive (6 bytes used):
       bytes 0-1: CommandedSpeed int16 LE (cm/s)
       byte  2:   Brake uint8 (0=off, 1=hold/12V, 2=on/24V)
       byte  3:   Mode  uint8 (0-7 — Nav-requested state; DBW arbitrates)
       bytes 4-5: CommandedSteerAngle int16 LE (deg x 10)
     0x100 HiStatus (1 byte used):
       byte 0 bits: 0x80=E-stop, 0x40=autonomous, 0x04=reverse,
                    0x02=reverse pending, 0x01=reverse unavailable
 ************************************************************************************/
void Vehicle::receiveCan() {
  if (startupMode == Startup::Diagnostic) return;
  while (Can0.available() > 0) {
    Can0.read(incoming);
    if (incoming.id == HiDrive_CANID) {
      desired_speed_cmPs   = incoming.data.int16[0];   // bytes 0-1
      desired_brake        = incoming.data.uint8[2];   // byte 2 (was int16 — wiki spec is uint8)
      desired_angle_DegX10 = incoming.data.int16[2];   // bytes 4-5
      // Buffer raw Nav-sent values for 0x704 Log_auto emit.
      last_nav_speed_cmPs   = desired_speed_cmPs;
      last_nav_brake        = incoming.data.uint8[2];
      last_nav_mode         = incoming.data.uint8[3];  // byte 3 mode (informational)
      last_nav_angle_DegX10 = desired_angle_DegX10;
      canActive = true;
      // Explicit proof of 0x350 reception. Throttled to ~1s so it doesn't flood.
      static uint32_t lastNavRxDbg_ms = 0;
      // Only print when the USB TX buffer has room so an unread host can't
      // block the loop; availableForWrite() gating makes this non-blocking.
      if (SerialUSB && SerialUSB.availableForWrite() >= 96 && millis() - lastNavRxDbg_ms > 1000) {
        lastNavRxDbg_ms = millis();
        SerialUSB.print("# DBW GOT 0x350 from Nav: speed=");
        SerialUSB.print(desired_speed_cmPs);
        SerialUSB.print(" brake=");
        SerialUSB.print((int)desired_brake);
        SerialUSB.print(" angle=");
        SerialUSB.println(desired_angle_DegX10);
      }
    } else if (incoming.id == HiStatus_CANID) {
      uint8_t status = incoming.data.uint8[0];
      last_nav_status = status;
      if (status & 0x80) {
        currentAutoMode = ESTOP_RC;                    // 0x80 E-stop
      } else {
        currentAutoMode = (status & 0x40) ? AUTO_RC    // 0x40 set  -> autonomous
                                          : MANUAL_MODE;  // 0x40 clear -> manual
      }
      currentDriveMode = (status & 0x04) ? REVERSE_MODE : FORWARD_MODE;
      canActive = true;
    } else if (incoming.id == SimSteerActual_CANID && incoming.length >= 2) {
      // Simulator-only: Router's simulated actual wheel angle. Used in the
      // closed steering loop in place of L_SENSE/R_SENSE analog reads.
      // On the real trike this frame is not present and DBW will keep its
      // last-seen value at 0 (and SteeringController will fall back to the
      // analog read path).
      measured_wheel_angle_DegX10 = incoming.data.int16[0];
    }
  }
}
