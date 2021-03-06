#include <stdio.h>
#include <Arduino.h>
#include "stepper.h"
#include "shr16.h"
#include "main.h"
#include "mmctl.h"
#include "Buttons.h"
#include "permanent_storage.h"
#include "trinamic.h"

int8_t filament_type[EXTRUDERS] = {-1, -1, -1, -1, -1};
static bool isIdlerParked = false;

static const int idler_steps_after_homing = -130;

static const int idler_steps = 1420 / 4;    // 2 msteps = 180 / 4
static const int idler_parking_steps = (idler_steps / 2) + 80;  // 40


static int set_idler_direction(int _steps);
static int set_pulley_direction(int _steps);
static void set_idler_dir_down();
static void set_idler_dir_up();
static void move(int _idler, int _pulley);

//! @brief Compute steps for idler needed to change filament
//! @param current_filament Currently selected filament
//! @param next_filament Filament to be selected
//! @return idler steps
int get_idler_steps(int current_filament, int next_filament)
{
    return ((current_filament - next_filament) * idler_steps);
}

void do_pulley_step()
{
    pulley_step_pin_set();
	asm("nop");
	pulley_step_pin_reset();
	asm("nop");
}

//! @brief home idler
//!
//! @param toLastFilament
//!   - true
//! Move idler to previously loaded filament and disengage. Returns true.
//! Does nothing if last filament used is not known and returns false.
//!   - false (default)
//! Move idler to filament 0 and disengage. Returns true.
//!
//! @retval true Succeeded
//! @retval false Failed
bool home_idler()
{
  int _c = 0;
  int _l = 0;
  // uint16_t val;
  tmc_init_axis(idler, AX_IDL, HOMING_MODE);

  move(-120, 0); // move a bit in opposite direction
  delay(100);
  idler_diag=0;

  for (int i = 0; i < 2200; i++)
  {
    if (idler_diag) { break; }
    move(1, 0);

    // val= tmc_read_sg(idler)>>1;

    _c++;
    if (i == 1000) { _l++; }
    if (_c > 100)
    { shr16_set_led(1 << 2 * _l); }
    if (_c > 200)
    { shr16_set_led(0x000); _c = 0; }
  }

  delay(300);
  tmc_init_axis(idler, AX_IDL, (TMC_MODE)tmc_mode);

  move(idler_steps_after_homing, 0); // move to initial position

  isIdlerParked = false;

  park_idler(false);

  return true;
}

//! @brief Home both idler if already not done
void home()
{
    home_idler();

    shr16_set_led(0x155);

    shr16_set_led(0x000);

    shr16_set_led(1 << 2 * (4-active_extruder));
}
 

void move_proportional(int _idler)
{
	// gets steps to be done and set direction
	_idler = set_idler_direction(_idler);

	float _idler_step = 1.0;
	float _idler_pos = 0;
	int delay = 2500; //microstep period in microseconds

	while ( _idler != 0 )
	{
		if (_idler_pos >= 1)
		{
			if (_idler > 0) { idler_step_pin_set(); }
		}

		asm("nop");
		
		if (_idler_pos >= 1)
		{
			if (_idler > 0) { idler_step_pin_reset(); _idler--;  }
		}

		asm("nop");

		if (_idler_pos >= 1)
		{
			_idler_pos = _idler_pos - 1;
		}


		_idler_pos = _idler_pos + _idler_step;

		delayMicroseconds(delay);
		if (delay > 900) { delay -= 10; }
		if (delay < 2500) { delay += 10; }

	}
}

void move(int _idler, int _pulley)
{
	int _acc = 50;

	// gets steps to be done and set direction
	_idler = set_idler_direction(_idler); 
	_pulley = set_pulley_direction(_pulley);
	

	do
	{
		if (_idler > 0) { idler_step_pin_set(); }
		if (_pulley > 0) { pulley_step_pin_set(); }
		asm("nop");
		if (_idler > 0) { idler_step_pin_reset(); _idler--; delayMicroseconds(1000); }
		if (_pulley > 0) { pulley_step_pin_reset(); _pulley--;  delayMicroseconds(700); }
		asm("nop");

		if (_acc > 0) { delayMicroseconds(_acc*10); _acc = _acc - 1; }; // super pseudo acceleration control

	} while (_idler != 0 || _pulley != 0);
}


void set_idler_dir_down()
{
	shr16_set_dir(shr16_get_dir() & ~4);
	//shr16_set_dir(shr16_get_dir() | 4);
}
void set_idler_dir_up()
{
	shr16_set_dir(shr16_get_dir() | 4);
	//shr16_set_dir(shr16_get_dir() & ~4);
}


int set_idler_direction(int _steps)
{
	if (_steps < 0)
	{
		_steps = _steps * -1;
		set_idler_dir_down();
	}
	else 
	{
		set_idler_dir_up();
	}
	return _steps;
}

int set_pulley_direction(int _steps)
{
	if (_steps < 0)
	{
		_steps = _steps * -1;
		set_pulley_dir_pull();
	}
	else
	{
		set_pulley_dir_push();
	}
	return _steps;
}

void set_pulley_dir_push()
{
	shr16_set_dir(shr16_get_dir() & ~1);
}
void set_pulley_dir_pull()
{
	shr16_set_dir(shr16_get_dir() | 1);
}

//! @brief Park idler
//! each filament selected has its park position, there is no park position for all filaments.
//! @param _unpark
//!  * false park
//!  * true engage
void park_idler(bool _unpark)
{
    if (_unpark && isIdlerParked) // get idler in contact with filament
    {
        move_proportional(idler_parking_steps);
        isIdlerParked = false;
    }
    else if (!_unpark && !isIdlerParked) // park idler so filament can move freely
    {
        move_proportional(idler_parking_steps*-1);
        isIdlerParked = true;
    }
}
