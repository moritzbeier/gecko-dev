/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.glean.private

import androidx.annotation.VisibleForTesting
import mozilla.components.service.glean.Dispatchers
import mozilla.components.service.glean.error.ErrorRecording
import mozilla.components.service.glean.storages.TimespansStorageEngine
import mozilla.components.service.glean.timing.GleanTimerId
import mozilla.components.service.glean.timing.TimingManager
import mozilla.components.support.base.log.logger.Logger

/**
 * This implements the developer facing API for recording timespans.
 *
 * Instances of this class type are automatically generated by the parsers at build time,
 * allowing developers to record values that were previously registered in the metrics.yaml file.
 *
 * The timespan API exposes the [start], [stop] and [cancel] methods.
 */
data class TimespanMetricType(
    override val disabled: Boolean,
    override val category: String,
    override val lifetime: Lifetime,
    override val name: String,
    override val sendInPings: List<String>,
    val timeUnit: TimeUnit
) : CommonMetricData {

    private val logger = Logger("glean/TimespanMetricType")

    // The identifier for the timer managed by this metric.
    private var timerId: GleanTimerId? = null

    /**
     * Start tracking time for the provided metric.
     * This records an error if it’s already tracking time (i.e. start was already
     * called with no corresponding [stop]): in that case the original
     * start time will be preserved.
     */
    fun start() {
        if (!shouldRecord(logger)) {
            return
        }

        if (timerId != null) {
            ErrorRecording.recordError(
                    this,
                    ErrorRecording.ErrorType.InvalidValue,
                    "Timespan already started",
                    logger
            )
            return
        }

        timerId = TimingManager.start(this)
    }

    /**
     * Stop tracking time for the provided metric.
     * Sets the metric to the elapsed time.This will record
     * an error if no [start] was called.
     */
    fun stop() {
        if (!shouldRecord(logger)) {
            return
        }

        if (timerId == null) {
            ErrorRecording.recordError(
                    this,
                    ErrorRecording.ErrorType.InvalidValue,
                    "Timespan not running",
                    logger
            )
            return
        }

        timerId?.let { id ->
            TimingManager.stop(this, id)?.let { elapsedNanos ->
                @Suppress("EXPERIMENTAL_API_USAGE")
                Dispatchers.API.launch {
                    TimespansStorageEngine.set(this@TimespanMetricType, timeUnit, elapsedNanos)

                    // Reset the timerId.
                    timerId = null
                }
            }
        }
    }

    /**
     * Abort a previous [start] call. No error is recorded if no [start] was called.
     */
    fun cancel() {
        if (!shouldRecord(logger)) {
            return
        }

        timerId?.let {
            TimingManager.cancel(this, it)
        }

        // Reset the timerId.
        timerId = null
    }

    /**
     * Explicitly set the timespan value, in nanoseconds.
     *
     * This API should only be used if your library or application requires recording
     * times in a way that can not make use of [start]/[stop]/[cancel].
     *
     * [setRawNanos] does not overwrite a running timer or an already existing value.
     *
     * @param elapsedNanos The elapsed time to record, in nanoseconds.
     */
    fun setRawNanos(elapsedNanos: Long) {
        if (!shouldRecord(logger)) {
            return
        }

        if (timerId != null) {
            ErrorRecording.recordError(
                    this,
                    ErrorRecording.ErrorType.InvalidValue,
                    "Timespan already running. Raw value not recorded.",
                    logger
            )
            return
        }

        @Suppress("EXPERIMENTAL_API_USAGE")
        Dispatchers.API.launch {
            TimespansStorageEngine.set(
                this@TimespanMetricType,
                timeUnit,
                elapsedNanos
            )
        }
    }

    /**
     * Tests whether a value is stored for the metric for testing purposes only
     *
     * @param pingName represents the name of the ping to retrieve the metric for.  Defaults
     *                 to the either the first value in [defaultStorageDestinations] or the first
     *                 value in [sendInPings]
     * @return true if metric value exists, otherwise false
     */
    @VisibleForTesting(otherwise = VisibleForTesting.NONE)
    fun testHasValue(pingName: String = sendInPings.first()): Boolean {
        return TimespansStorageEngine.getSnapshotWithTimeUnit(pingName, false)?.get(identifier) != null
    }

    /**
     * Returns the stored value for testing purposes only, in the metric's time unit.
     *
     * @param pingName represents the name of the ping to retrieve the metric for.  Defaults
     *                 to the either the first value in [defaultStorageDestinations] or the first
     *                 value in [sendInPings]
     * @return value of the stored metric
     * @throws [NullPointerException] if no value is stored
     */
    @VisibleForTesting(otherwise = VisibleForTesting.NONE)
    fun testGetValue(pingName: String = sendInPings.first()): Long {
        return TimespansStorageEngine.getSnapshotWithTimeUnit(pingName, false)!![identifier]!!.second
    }
}
