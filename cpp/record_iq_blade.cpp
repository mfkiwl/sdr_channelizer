/* Save to a file, e.g. boilerplate.c, and then compile:
* $ gcc boilerplate.c -o libbladeRF_example_boilerplate -lbladeRF
*/
#include <libbladeRF.h>

#include "IqPacket.h"

#include <cstring>
#include <ctime>

#include <bit>
#include <iostream>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <vector>
#include <iterator>
#include <execution>

void getFilenameStr(char* filenameStr)
{
	// Get current time
	const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
	// Convert current time from chrono to time_t which goes down to second precision
	const std::time_t tt = std::chrono::system_clock::to_time_t(now);
	// Convert back to chrono so that we have a current time rounded to seconds
	const std::chrono::system_clock::time_point nowSec = std::chrono::system_clock::from_time_t(tt);
	tm utc_tm = *gmtime(&tt);

	std::uint16_t year  = utc_tm.tm_year + 1900;
	std::uint8_t month  = utc_tm.tm_mon + 1;
	std::uint8_t day    = utc_tm.tm_mday;
	std::uint8_t hour   = utc_tm.tm_hour;
	std::uint8_t minute = utc_tm.tm_min;
	std::uint8_t second = utc_tm.tm_sec;
	std::uint16_t millisecond = (now-nowSec)/std::chrono::milliseconds(1);

	snprintf(filenameStr, 80, "%04d_%02d_%02d_%02d_%02d_%02d_%03d.iq", year, month, day, hour, minute, second, millisecond);
}

int main(const int argc, const char *argv[])
{
	std::int32_t status;
	bladerf *dev = NULL;
	bladerf_devinfo dev_info;
	bladerf_metadata meta;
	bladerf_channel channel = BLADERF_CHANNEL_RX(0);
	struct bladerf_version version;
	bladerf_serial serNo;
	IqPacket packet;
	char filenameStr[80];
	const std::int16_t SAMP_MAX = 2047;
	const std::int16_t SAMP_MIN = -2048;
	bool saturated = false;
	std::uint32_t overrunCounter = 0;

	const std::uint64_t frequencyHz = atof(argv[1])*1e6;
	const std::uint32_t requestedBandwidthHz = atof(argv[2])*1e6;
	std::uint32_t receivedBandwidthHz = 0;
	const std::uint32_t requestedSampleRate = atof(argv[3])*1e6;
	std::uint32_t receivedSampleRate = 0;
	std::int32_t rxGain = atoi(argv[4]);
	const float dwellDuration = atof(argv[5]);
	const float collectionDuration = atof(argv[6]);

	/* Initialize the information used to identify the desired device
	* to all wildcard (i.e., "any device") values */
	bladerf_init_devinfo(&dev_info);

	status = bladerf_open_with_devinfo(&dev, &dev_info);

	if (status != 0)
	{
		std::cout << "Unable to open device: " << bladerf_strerror(status) << std::endl;
		return 1;
	}
	
	packet.linkSpeed = bladerf_device_speed(dev);
	
	if ( packet.linkSpeed == BLADERF_DEVICE_SPEED_SUPER )
	{
		std::cout << "Negotiated USB 3 link speed" << std::endl;
	}
	else if ( packet.linkSpeed == BLADERF_DEVICE_SPEED_HIGH )
	{
		std::cout << "Negotiated USB 2 link speed" << std::endl;
	}
	else
	{
		std::cout << "Negotiated unknown link speed" << std::endl;
	}

	bladerf_fpga_version(dev, &version);

	strncpy(packet.fpgaVersion, version.describe, sizeof(packet.fpgaVersion));

	std::cout << "FPGA version: " << packet.fpgaVersion << std::endl;

	bladerf_fw_version(dev, &version);

	strncpy(packet.fwVersion, version.describe, sizeof(packet.fwVersion));

	std::cout << "Firmware version: " << packet.fwVersion << std::endl;

	bladerf_get_serial_struct(dev, &serNo);

	std::cout << "Using " << bladerf_get_board_name(dev) << " serial number " << serNo.serial << std::endl;

	// Set center frequency of device

	status = bladerf_set_frequency(dev, channel, frequencyHz);

	if (status == 0)
	{
		std::cout << "Frequency = " << frequencyHz*1e-6 << " MHz" << std::endl;
	}
	else
	{
		std::cout << "Failed to set frequency = " << frequencyHz << ": " << bladerf_strerror(status) << std::endl;
		bladerf_close(dev);
		return 1;
	}

	// Set sample rate of device

	status = bladerf_set_sample_rate(dev, channel, requestedSampleRate, &receivedSampleRate);

	if (status == 0)
	{
		std::cout <<  "Sample Rate = " << receivedSampleRate*1e-6 << " Msps" << std::endl;
	}
	else
	{
		std::cout <<  "Failed to set samplerate = " << requestedSampleRate << ": " << bladerf_strerror(status) << std::endl;
		bladerf_close(dev);
		return 1;
	}

	// Set analog bandwidth of device

	status = bladerf_set_bandwidth(dev, channel, requestedBandwidthHz, &receivedBandwidthHz);

	if (status == 0)
	{
		std::cout << "Bandwidth = " << receivedBandwidthHz*1e-6 << " MHz" << std::endl;
	}
	else
	{
		std::cout << "Failed to set bandwidth = " << requestedBandwidthHz << ": " << bladerf_strerror(status) << std::endl;
		bladerf_close(dev);
		return 1;
	}

	// Disable automatic gain control

	status = bladerf_set_gain_mode(dev, channel, BLADERF_GAIN_MGC);

	if (status == 0)
	{
		std::cout << "Disabled automatic gain control" << std::endl;
	}
	else
	{
		std::cout << "Failed to disable automatic gain control: " << bladerf_strerror(status) << std::endl;
		bladerf_close(dev);
		return 1;
	}

	// Set gain of the device

	status = bladerf_set_gain(dev, channel, rxGain);

	if (status == 0)
	{
		std::cout << "Gain = " << rxGain << " dB" << std::endl;
	}
	else
	{
		std::cout << "Failed to set gain: " << bladerf_strerror(status) << std::endl;
		bladerf_close(dev);
		return 1;
	}

	// Compute the requested number of samples and buffer size

	const std::uint32_t sampleLength = dwellDuration*receivedSampleRate;
	const std::uint32_t bufferSize = 2*sampleLength;

	// Set up the configuration parameters necessary to receive samples with the device

	/* These items configure the underlying asynch stream used by the sync
	* interface. The "buffer" here refers to those used internally by worker
	* threads, not the user's sample buffers.
	*
	* It is important to remember that TX buffers will not be submitted to
	* the hardware until `buffer_size` samples are provided via the
	* bladerf_sync_tx call. Similarly, samples will not be available to
	* RX via bladerf_sync_rx() until a block of `buffer_size` samples has been
	* received. */
	const std::uint32_t num_buffers = 4;
	const std::uint32_t buffer_size = 1024 * 1024; /* Must be a multiple of 1024 */
	const std::uint32_t num_transfers = 2;
	const std::uint32_t timeout_ms = 3500;

	/* Configure both the device's x1 RX and TX channels for use with the
	* synchronous
	* interface. SC16 Q11 samples *with* metadata are used. */
	status = bladerf_sync_config(dev, BLADERF_RX_X1, BLADERF_FORMAT_SC16_Q11_META, num_buffers, buffer_size, num_transfers, timeout_ms);

	if (status == 0)
	{
		std::cout << "Configured RX sync interface" << std::endl;
	}
	else
	{
		std::cout << "Failed to configure RX sync interface: " << bladerf_strerror(status) << std::endl;
		bladerf_close(dev);
		return 1;
	}

	status = bladerf_enable_module(dev, BLADERF_RX, true);

	if (status == 0)
	{
		std::cout << "Enabled RX" << std::endl;
	}
	else
	{
		std::cout << "Failed to enable RX: " << bladerf_strerror(status) << std::endl;
		bladerf_close(dev);
	}

	// Specify the endianness of the recording

	if constexpr (std::endian::native == std::endian::big)
	{
		packet.endianness = 0x00000000;
	}
	else if constexpr (std::endian::native == std::endian::little)
	{
		packet.endianness = 0x01010101;
	}
	else
	{
		packet.endianness = 0xFFFFFFFF;
	}

	// Set information about the recording for data analysis purposes

	packet.frequencyHz = frequencyHz;
	packet.bandwidthHz = receivedBandwidthHz;
	packet.sampleRate = receivedSampleRate;
	packet.numSamples = sampleLength;

	// Allocate the host buffer the device will be streaming to

	std::vector<std::int16_t> iq_vec;
	iq_vec.resize(bufferSize, 0);
	std::int16_t* iq = iq_vec.data();

	const std::chrono::system_clock::time_point startTime = std::chrono::system_clock::now();
	std::chrono::system_clock::time_point currentTime;

	do
	{
		// If we're saturated, then drop the receive gain down by 1 dB
		if (saturated)
		{
			status = bladerf_set_gain(dev, channel, --rxGain);

			if (status == 0)
			{
				std::cout << "Gain = " << rxGain << " dB" << std::endl;
			}
			else
			{
				std::cout << "Failed to set gain: " << bladerf_strerror(status) << std::endl;
				bladerf_close(dev);
				return 1;
			}
		}

		packet.rxGain = rxGain;
		saturated = false;

		memset(iq, 0, bufferSize*sizeof(std::int16_t));

		memset(&meta, 0, sizeof(meta));
		meta.flags = BLADERF_META_FLAG_RX_NOW;

		packet.sampleStartTime = (std::chrono::system_clock::now().time_since_epoch() / std::chrono::nanoseconds(1)) * 1e-9;

		status = bladerf_sync_rx(dev, iq, sampleLength, &meta, 5000);

		if (status != 0)
		{
			std::cout << "RX \"now\" failed: " << bladerf_strerror(status) << std::endl;
		}
		else if (meta.status & BLADERF_META_STATUS_OVERRUN)
		{
			std::cout << "Overrun detected. " << meta.actual_count << " valid samples were read." << std::endl;
			overrunCounter++;
		}
		else
		{
			std::cout << "Received " << meta.actual_count << std::endl;

			// Look for instances of saturating to min or max value
			const auto [minSamp, maxSamp] = std::minmax_element(std::execution::par_unseq, std::begin(iq_vec), std::end(iq_vec));

			saturated = (((*minSamp) <= SAMP_MIN) || ((*maxSamp) >= SAMP_MAX));
		}

		packet.numSamples = meta.actual_count;

		getFilenameStr(filenameStr);

		std::ofstream fout(filenameStr);
		fout.write((char*)&packet, sizeof(packet));
		fout.write((char*)iq, 2*meta.actual_count*sizeof(std::int16_t));
		fout.close();

		currentTime = std::chrono::system_clock::now();
	}
	while(((currentTime - startTime) / std::chrono::milliseconds(1) * 1e-3) <= collectionDuration);

	// Disable the device

	status = bladerf_enable_module(dev, BLADERF_RX, false);

	if (status == 0)
	{
		std::cout << "Disabled RX" << std::endl;
	}
	else
	{
		std::cout << "Failed to disable RX: " << bladerf_strerror(status) << std::endl;
	}

	std::cout << "There were " << overrunCounter << " overruns." << std::endl;

	return status;
}
