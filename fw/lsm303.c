/**
 * This is most complex and difficult device.
 * It realize "read through write" paradigm. This is not standard, but
 * most of I2C devices use this paradigm.
 * You must write to device reading address, send restart to bus,
 * and then begin reading process.
 */

#include <stdlib.h>

#include "ch.h"
#include "hal.h"

#include "lsm303.h"

#define ACC_WA_SIZE    THD_WA_SIZE(256)
#define MAG_WA_SIZE    THD_WA_SIZE(256)

static i2cflags_t errors = 0;

acc_data_t acc_data =
  {0, 0, 0, 0};
acc_data_t mag_data =
  {0, 0, 0, 0};

uint8_t status_a;
uint8_t status_m;

/* Threads. */
static Thread *acctp = NULL;
static Thread *magtp = NULL;

/**
 * Init function.
 */
int lsm303_acc_init(I2CDriver *i2cp) {
  uint8_t rx_data[LSM303_RX_DEPTH];
  uint8_t tx_data[LSM303_TX_DEPTH];
  msg_t status = RDY_OK;
  systime_t timeout = MS2ST(4);

  /* configure accelerometer */
  tx_data[0] = LSM303_CTRL_REG1_A | LSM303_AUTO_INCREMENT; /* register address */
  tx_data[1] = 0x67;
  tx_data[2] = 0x80;
  tx_data[3] = 0x10;
  tx_data[4] = 0x08;
  tx_data[5] = 0x08;
  tx_data[6] = 0x00;

  /* sending */
  i2cAcquireBus(i2cp);
  status = i2cMasterTransmitTimeout(i2cp, LSM303_ADDR_A, tx_data, 7, rx_data, 0,
                                    timeout);
  i2cReleaseBus(i2cp);

  if (status != RDY_OK) {
    errors = i2cGetErrors(i2cp);
    while (1)
      ;
  }

  return 0;
}

/**
 *
 */
void lsm303_acc_update(I2CDriver *i2cp) {
  uint8_t rx_data[LSM303_RX_DEPTH];
  uint8_t tx_data[LSM303_TX_DEPTH];
  msg_t status = RDY_OK;
  systime_t tmo = MS2ST(4);

  acc_data.t = chTimeNow();
  tx_data[0] = LSM303_OUT_X_L_A | LSM303_AUTO_INCREMENT; /* register address */
  i2cAcquireBus(i2cp);
  status = i2cMasterTransmitTimeout(i2cp, LSM303_ADDR_A, tx_data, 1, rx_data, 6,
                                    tmo);
  i2cReleaseBus(i2cp);

  if (status != RDY_OK) {
    errors = i2cGetErrors(i2cp);
    palClearPad(LED_GPIO, LED1);
    palClearPad(LED_GPIO, LED2);
    palClearPad(LED_GPIO, LED3);
    palClearPad(LED_GPIO, LED4);
    while (1)
      ;
  }

  acc_data.x = (int16_t)((rx_data[1] << 8) | rx_data[0]) / 16;
  acc_data.y = (int16_t)((rx_data[3] << 8) | rx_data[2]) / 16;
  acc_data.z = (int16_t)((rx_data[5] << 8) | rx_data[4]) / 16;

  tx_data[0] = LSM303_STATUS_REG_A; /* register address */
  i2cAcquireBus(i2cp);
  status = i2cMasterTransmitTimeout(i2cp, LSM303_ADDR_A, tx_data, 1, rx_data, 2,
                                    tmo);
  i2cReleaseBus(i2cp);

  if (status != RDY_OK) {
    errors = i2cGetErrors(i2cp);
    palClearPad(LED_GPIO, LED1);
    palClearPad(LED_GPIO, LED2);
    palClearPad(LED_GPIO, LED3);
    palClearPad(LED_GPIO, LED4);
    while (1)
      ;
  }

  status_a = rx_data[0];
}

void lsm303_acc_int1_cb(void) {
  /* Wakes up the thread.*/
  chSysLockFromIsr();
  if (acctp != NULL) {
    acctp->p_u.rdymsg = (msg_t)LSM303_ACC_DATA_READY;
    chSchReadyI(acctp);
    acctp = NULL;
  }
  chSysUnlockFromIsr();
}

static msg_t lsm303_acc_update_thread(void *p) {
  I2CDriver *i2cp = (I2CDriver *)p;

  while (TRUE) {
    msg_t msg;

    /* Waiting for the IRQ to happen.*/

    chSysLock();
    acctp = chThdSelf();
    chSchGoSleepS(THD_STATE_SUSPENDED);
    msg = chThdSelf()->p_u.rdymsg;
    chSysUnlock();

    /* If data ready, update all axis.*/
    if (msg == LSM303_ACC_DATA_READY) {
      palClearPad(LED_GPIO, LED3);
      lsm303_acc_update(i2cp);
      while (status_a != 0x00) {
        lsm303_acc_update(i2cp);
      }
      palSetPad(LED_GPIO, LED3);
//      chprintf((BaseChannel *)&SERIAL_DRIVER, "ACC T: %d X: %d Y: %d Z: %d\r\n",
//                 acc_update_time, acc_data.x, acc_data.y, acc_data.z);
    }
  }

  return RDY_OK;
}

/**
 * Init function.
 */
int lsm303_mag_init(I2CDriver *i2cp) {
  uint8_t rx_data[LSM303_RX_DEPTH];
  uint8_t tx_data[LSM303_TX_DEPTH];
  msg_t status = RDY_OK;
  systime_t timeout = MS2ST(4);

  /* configure accelerometer */
  tx_data[0] = LSM303_CRA_REG_M; /* register address */
  tx_data[1] = 0x1C;
  tx_data[2] = 0x20;
  tx_data[3] = 0x00;

  /* sending */
  i2cAcquireBus(i2cp);
  status = i2cMasterTransmitTimeout(i2cp, LSM303_ADDR_M, tx_data, 4, rx_data, 0,
                                    timeout);
  i2cReleaseBus(i2cp);

  if (status != RDY_OK) {
    errors = i2cGetErrors(i2cp);
    while (1)
      ;
  }

  return 0;
}

/**
 *
 */
void lsm303_mag_update(I2CDriver *i2cp) {
  uint8_t rx_data[LSM303_RX_DEPTH];
  uint8_t tx_data[LSM303_TX_DEPTH];
  msg_t status = RDY_OK;
  systime_t tmo = MS2ST(4);

  acc_data.t = chTimeNow();
  tx_data[0] = LSM303_OUT_X_H_M; /* register address */
  i2cAcquireBus(i2cp);
  status = i2cMasterTransmitTimeout(i2cp, LSM303_ADDR_M, tx_data, 1, rx_data, 6,
                                    tmo);
  i2cReleaseBus(i2cp);

  if (status != RDY_OK) {
    errors = i2cGetErrors(i2cp);
    palClearPad(LED_GPIO, LED1);
    palClearPad(LED_GPIO, LED2);
    palClearPad(LED_GPIO, LED3);
    palClearPad(LED_GPIO, LED4);
    while (1)
      ;
  }

  mag_data.x = (int16_t)((rx_data[0] << 8) | rx_data[1]);
  mag_data.z = (int16_t)((rx_data[2] << 8) | rx_data[3]);
  mag_data.y = (int16_t)((rx_data[4] << 8) | rx_data[5]);

  /*
  tx_data[0] = LSM303_SR_REG_M;
  i2cAcquireBus(i2cp);
  status = i2cMasterTransmitTimeout(i2cp, LSM303_ADDR_M, tx_data, 1, rx_data, 2,
                                    tmo);
  i2cReleaseBus(i2cp);

  if (status != RDY_OK) {
    errors = i2cGetErrors(i2cp);
    palClearPad(LED_GPIO, LED1);
    palClearPad(LED_GPIO, LED2);
    palClearPad(LED_GPIO, LED3);
    palClearPad(LED_GPIO, LED4);
    while (1)
      ;
  }

  status_m = rx_data[0];
  */
}

void lsm303_mag_drdy_cb(void) {
  /* Wakes up the thread.*/
  chSysLockFromIsr();
  if (magtp != NULL) {
    magtp->p_u.rdymsg = (msg_t)LSM303_MAG_DATA_READY;
    chSchReadyI(magtp);
    magtp = NULL;
  }
  chSysUnlockFromIsr();
}

static msg_t lsm303_mag_update_thread(void *p) {
  I2CDriver *i2cp = (I2CDriver *)p;

  while (TRUE) {
    msg_t msg;

    /* Waiting for the IRQ to happen.*/

    chSysLock();
    magtp = chThdSelf();
    chSchGoSleepS(THD_STATE_SUSPENDED);
    msg = chThdSelf()->p_u.rdymsg;
    chSysUnlock();

    /* If data ready, update all axis.*/
    if (msg == LSM303_MAG_DATA_READY) {
      palClearPad(LED_GPIO, LED4);
      lsm303_mag_update(i2cp);
//      if (status_m != 0x00) {
//        lsm303_mag_update(i2cp);
//      }
      palSetPad(LED_GPIO, LED4);
    }
  }

  return RDY_OK;
}

/*
 * Threads.
 */

Thread *accRun(I2CDriver *i2cp, tprio_t prio) {
  Thread *tp;

  lsm303_acc_init(i2cp);
  chThdSleepMilliseconds(200);
  lsm303_acc_update(i2cp);
  tp = chThdCreateFromHeap(NULL, ACC_WA_SIZE, prio, lsm303_acc_update_thread,
                           (void*)i2cp);
  extChannelEnable(&EXTD1, AM_INT1);

  return tp;
}

Thread *magRun(I2CDriver *i2cp, tprio_t prio) {
  Thread *tp;

  lsm303_mag_init(i2cp);
  chThdSleepMilliseconds(200);
  lsm303_mag_update(i2cp);
  tp = chThdCreateFromHeap(NULL, MAG_WA_SIZE, prio, lsm303_mag_update_thread,
                           (void*)i2cp);
  extChannelEnable(&EXTD1, AM_DRDY);

  return tp;
}
