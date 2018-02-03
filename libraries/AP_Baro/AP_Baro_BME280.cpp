/*
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "AP_Baro_BME280.h"

#include <utility>

extern const AP_HAL::HAL &hal;

#define BME280_MODE_SLEEP  0
#define BME280_MODE_FORCED 1
#define BME280_MODE_NORMAL 3
#define BME280_MODE BME280_MODE_NORMAL

#define BME280_OVERSAMPLING_1  1
#define BME280_OVERSAMPLING_2  2
#define BME280_OVERSAMPLING_4  3
#define BME280_OVERSAMPLING_8  4
#define BME280_OVERSAMPLING_16 5
#define BME280_OVERSAMPLING_P BME280_OVERSAMPLING_16
#define BME280_OVERSAMPLING_T BME280_OVERSAMPLING_2

#define BME280_FILTER_COEFFICIENT 2

#define BME280_ID            0x60

#define BME280_REG_CALIB     0x88
#define BME280_REG_CALIB2    0xE1
#define BME280_REG_ID        0xD0
#define BME280_REG_RESET     0xE0
#define BME280_REG_STATUS    0xF3
#define BME280_REG_CTRL_MEAS 0xF4
#define BME280_REG_CONFIG    0xF5
#define BME280_REG_DATA      0xF7

AP_Baro_BME280::AP_Baro_BME280(AP_Baro &baro, AP_HAL::OwnPtr<AP_HAL::Device> dev)
    : AP_Baro_Backend(baro)
    , _dev(std::move(dev))
{
}


AP_Baro_Backend *AP_Baro_BME280::probe(AP_Baro &baro,
                                       AP_HAL::OwnPtr<AP_HAL::Device> dev)
{
    if (!dev) {
        return nullptr;
    }

    AP_Baro_BME280 *sensor = new AP_Baro_BME280(baro, std::move(dev));
    if (!sensor || !sensor->_init()) {
        delete sensor;
        return nullptr;
    }
    return sensor;
}

bool AP_Baro_BME280::_init()
{
    if (!_dev | !_dev->get_semaphore()->take(HAL_SEMAPHORE_BLOCK_FOREVER)) {
        return false;
    }

    _has_sample = false;

    _dev->set_speed(AP_HAL::Device::SPEED_HIGH);

    uint8_t whoami;
    if (!_dev->read_registers(BME280_REG_ID, &whoami, 1)  ||
        whoami != BME280_ID) {
        // not a BME280
        _dev->get_semaphore()->give();
        return false;
    }

    // read the calibration data
    uint8_t buf[26];	// 0x88-0xa1 inclusive
    _dev->read_registers(BME280_REG_CALIB, buf, sizeof(buf));

    _t1 = ((int16_t)buf[1] << 8) | buf[0];
    _t2 = ((int16_t)buf[3] << 8) | buf[2];
    _t3 = ((int16_t)buf[5] << 8) | buf[4];
    _p1 = ((int16_t)buf[7] << 8) | buf[6];
    _p2 = ((int16_t)buf[9] << 8) | buf[8];
    _p3 = ((int16_t)buf[11] << 8) | buf[10];
    _p4 = ((int16_t)buf[13] << 8) | buf[12];
    _p5 = ((int16_t)buf[15] << 8) | buf[14];
    _p6 = ((int16_t)buf[17] << 8) | buf[16];
    _p7 = ((int16_t)buf[19] << 8) | buf[18];
    _p8 = ((int16_t)buf[21] << 8) | buf[20];
    _p9 = ((int16_t)buf[23] << 8) | buf[22];
    _h1 = buf[25];

    // read the extra calibration data for humidity
    // 0xe1-0xf0
    _dev->read_registers(BME280_REG_CALIB2, buf, 8 /* magic number for just the _h values from page 22,23 */);

    _h2 = ((int16_t)buf[1] << 8) | buf[0];
    _h3 = buf[2];

    _h4 = ((int16_t)buf[3] << 4) | (0xf&buf[4]); // these get fugly on page 23
    _h5 = ((int16_t)buf[5] << 4) | (0xf&buf[4]); //

    _h6 = (int8_t) buf[6];


    // SPI write needs bit mask
    uint8_t mask = 0xFF;
    if (_dev->bus_type() == AP_HAL::Device::BUS_TYPE_SPI) {
        mask = 0x7F;
    }

    _dev->write_register((BME280_REG_CTRL_MEAS & mask), (BME280_OVERSAMPLING_T << 5) |
                         (BME280_OVERSAMPLING_P << 2) | BME280_MODE);

    _dev->write_register((BME280_REG_CONFIG & mask), BME280_FILTER_COEFFICIENT << 2);

    _instance = _frontend.register_sensor();

    _dev->get_semaphore()->give();

    // 31 Jan 18   JV	- Normally 50Hz update but for now let's start with 1 second
    // request 50Hz update
    _dev->register_periodic_callback(1000 * AP_USEC_PER_MSEC, FUNCTOR_BIND_MEMBER(&AP_Baro_BME280::_timer, void));

    return true;
}



//  acumulate a new sensor reading
void AP_Baro_BME280::_timer(void)
{
    uint8_t buf[8];

    _dev->read_registers(BME280_REG_DATA, buf, sizeof(buf));

    _update_temperature((buf[3] << 12) | (buf[4] << 4) | (buf[5] >> 4));
    _update_pressure((buf[0] << 12) | (buf[1] << 4) | (buf[2] >> 4));
    _update_humidity(buf[6]<<8 | buf[7]);
}

// transfer data to the frontend
// called from front end
void AP_Baro_BME280::update(void)
{
    if (_sem->take_nonblocking()) {
        if (!_has_sample) {
            _sem->give();
            return;
        }

	//        _copy_to_frontend(_instance, _pressure, _temperature, _humidity);
	Pressure = _pressure;
	Temperature = _temperature;
	Humidity = _humidity;

        _has_sample = false;
        _sem->give();
    }
}

// calculate temperature
void AP_Baro_BME280::_update_temperature(int32_t temp_raw)
{
    int32_t var1, var2, t;

    // according to datasheet page 22
    var1 = ((((temp_raw >> 3) - ((int32_t)_t1 << 1))) * ((int32_t)_t2)) >> 11;
    var2 = (((((temp_raw >> 4) - ((int32_t)_t1)) * ((temp_raw >> 4) - ((int32_t)_t1))) >> 12) * ((int32_t)_t3)) >> 14;
    _t_fine = var1 + var2;
    t = (_t_fine * 5 + 128) >> 8;
    if (_sem->take(HAL_SEMAPHORE_BLOCK_FOREVER)) {
        _temperature = ((float)t) / 100;
        _sem->give();
    }
}

// calculate pressure
void AP_Baro_BME280::_update_pressure(int32_t press_raw)
{
    int64_t var1, var2, p;

    // according to datasheet page 22
    var1 = ((int64_t)_t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)_p6;
    var2 = var2 + ((var1 * (int64_t)_p5) << 17);
    var2 = var2 + (((int64_t)_p4) << 35);
    var1 = ((var1 * var1 * (int64_t)_p3) >> 8) + ((var1 * (int64_t)_p2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)_p1) >> 33;

    if (var1 == 0) {
        return;
    }

    p = 1048576 - press_raw;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)_p9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)_p8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)_p7) << 4);

    if (_sem->take(HAL_SEMAPHORE_BLOCK_FOREVER)) {
        _pressure = (float)p / 25600;
        _has_sample = true;
        _sem->give();
    }
}


// calculate humidity
// Returns humidity in %RH as unsigned 32 bit integer in Q22.10 format (22 integer and 10 fractional bits).
// Output value of “47445” represents 47445/1024 = 46.333 %RH

void AP_Baro_BME280::_update_humidity(int32_t hum_raw)
{
  // assume temperate already done to get    _t_fine = var1 + var2;


    // From DATASHEET
    //
    //BME280_U32_t bme280_compensate_H_int32(BME280_S32_t adc_H)
    //### todo 31 Jan 18   JV	- remove the define
    #define BME280_S32_t int32_t
    BME280_S32_t   v_x1_u32r;

    v_x1_u32r = (_t_fine - ((BME280_S32_t)76800));
    v_x1_u32r = (((((hum_raw << 14) - (((BME280_S32_t)_h4) << 20) - (((BME280_S32_t)_h5) * v_x1_u32r)) +
		   ((BME280_S32_t)16384)) >> 15) * (((((((v_x1_u32r * ((BME280_S32_t)_h6)) >> 10) * (((v_x1_u32r * 
			    ((BME280_S32_t)_h3)) >> 11) + ((BME280_S32_t)32768))) >> 10) + ((BME280_S32_t)2097152)) *
						     ((BME280_S32_t)_h2) + 8192) >> 14));
    v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * ((BME280_S32_t)_h1)) >> 4));
    v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
    v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);
    //    return (BME280_U32_t)(v_x1_u32r>>12);
		 // end DATASHEET

    if (_sem->take(HAL_SEMAPHORE_BLOCK_FOREVER)) {
      float h = (v_x1_u32r>>12);
        _humidity = h / 1024.0;
        _sem->give();
    }



#ifdef FROM_ARDUINO
###########remove this#############
float Adafruit_BME280::readHumidity(void) {
    readTemperature(); // must be done first to get t_fine

    int32_t adc_H = read16(BME280_REGISTER_HUMIDDATA);
    if (adc_H == 0x8000) // value in case humidity measurement was disabled
        return NAN;
        
    int32_t v_x1_u32r;

    v_x1_u32r = (t_fine - ((int32_t)76800));

    v_x1_u32r = (((((adc_H << 14) - (((int32_t)_bme280_calib.dig_H4) << 20) -
                    (((int32_t)_bme280_calib.dig_H5) * v_x1_u32r)) + ((int32_t)16384)) >> 15) *
                 (((((((v_x1_u32r * ((int32_t)_bme280_calib.dig_H6)) >> 10) *
                      (((v_x1_u32r * ((int32_t)_bme280_calib.dig_H3)) >> 11) + ((int32_t)32768))) >> 10) +
                    ((int32_t)2097152)) * ((int32_t)_bme280_calib.dig_H2) + 8192) >> 14));

    v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) *
                               ((int32_t)_bme280_calib.dig_H1)) >> 4));

    v_x1_u32r = (v_x1_u32r < 0) ? 0 : v_x1_u32r;
    v_x1_u32r = (v_x1_u32r > 419430400) ? 419430400 : v_x1_u32r;
    float h = (v_x1_u32r>>12);
    return  h / 1024.0;
}

#endif



}


