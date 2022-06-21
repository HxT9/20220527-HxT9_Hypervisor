
/**
  * @brief The maximum wait before PAUSE
  *
  */
static unsigned MaxWait = 65536;

/**
 * @brief Tries to get the lock otherwise returns
 *
 * @param LONG Lock variable
 * @return BOOLEAN If it was successfull on getting the lock
 */
BOOLEAN SpinlockTryLock(volatile LONG* Lock);

/**
 * @brief Tries to get the lock and won't return until successfully get the lock
 *
 * @param LONG Lock variable
 */
void SpinlockLock(volatile LONG* Lock);

/**
 * @brief Interlocked spinlock that tries to change the value
 * and makes sure that it changed the target value
 *
 * @param Destination A pointer to the destination value
 * @param Exchange The exchange value
 * @param Comperand The value to compare to Destination
 */
void SpinlockInterlockedCompareExchange(LONG volatile* Destination, LONG Exchange, LONG Comperand);

/**
 * @brief Release the lock
 *
 * @param LONG Lock variable
 */
void SpinlockUnlock(volatile LONG* Lock);
