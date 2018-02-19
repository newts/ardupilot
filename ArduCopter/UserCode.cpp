#include "Copter.h"

#ifdef USERHOOK_INIT
void Copter::userhook_init()
{
    // put your initialisation code here
    // this will be called once at start-up
}
#endif

#ifdef USERHOOK_FASTLOOP
void Copter::userhook_FastLoop()
{
    // put your 100Hz code here
}
#endif

#ifdef USERHOOK_50HZLOOP
void Copter::userhook_50Hz()
{
    // put your 50Hz code here
}
#endif

#ifdef USERHOOK_MEDIUMLOOP
void Copter::userhook_MediumLoop()
{
    // put your 10Hz code here
}
#endif

#ifdef USERHOOK_SLOWLOOP
void Copter::userhook_SlowLoop()
{
    // put your 3.3Hz code here
  bar;
}
#endif

#ifdef USERHOOK_SUPERSLOWLOOP
void Copter::userhook_SuperSlowLoop()
{
    // put your 1Hz code here
  static int ticker = 0;

  if (++ticker > 1000) {
    ticker = 0;

    hal.console->printf("printf HAL\n"); // no
    fprintf(stderr, "printf stderr\n"); // yes
    gcs().send_text(MAV_SEVERITY_WARNING, "gcs send text"); // yes to Ground Station screen
    //    gcs().send_text(MAV_SEVERITY_WARNING, "Gas1 T,P,H: %f,%f,%f",Gas1.Temperature, Gas1.Pressure, Gas1.Humidity);
    //    const uint64_t now = AP_HAL::micros64();
    //    DataFlash.Log_Write("Gas1", "TimeUS,Temp,Pres,Hum", "QOff", now, Gas1.Temperature, Gas1.Pressure, Gas1.Humidity);
  }
}
#endif
