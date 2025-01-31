%% Clear everything out

clc

fprintf('%s - Clearing everything out\n', datestr(now))

clear all
close all

%% Load data

listing = dir(uigetdir('/'));

%% Initialize data

pdw.toa = [];
pdw.freq = [];
pdw.snr = [];
pdw.pw = [];
pdw.sat = [];

for ii = 1:length(listing)
    if contains(listing(ii).name,'.mat')

        fprintf('%s - Loading %s\n', datestr(now), listing(ii).name)

        load(fullfile(listing(ii).folder,listing(ii).name))

        % Set parameters for channelizer

        M = fs*1e-6; % 1 MHz channelizer bins

        channelizer = dsp.Channelizer(M);

        iq = double(iq); % Convert from int16 to double
        iq = iq/32768; % Normalize from -1 to 1
        iq = iq(1,:) + 1j*iq(2,:); % Convert to complex

        fprintf('%s - Channelizing data\n', datestr(now))

        binFreqs = centerFrequencies(channelizer,fs);

        if length(iq) ~= size(iq,1)
            % Convert from row vector to column vector. NOTE: Don't use the
            % notation iq' here because that tranposes and conjugates.
            iq = transpose(iq);
        end

        % Lop off excess samples to make length a multiple of the number of
        % channelizer bins
        lastSample = floor(length(iq)/M)*M;

        iq(lastSample+1:end) = [];

        % Channelize the data in to M bands
        iq = channelizer(iq);

        % Rotate the data so it's centered
        iq = fftshift(iq,2);

        fs = fs/M; % This is the new decimated sampling rate

        fprintf('%s - Computing noise floor\n', datestr(now))

        % Convert complex I/Q to magnitude and phase data
        mag = abs(iq);
        phase = rad2deg(angle(iq));

        % Find the median magnitude value and set that as the noise floor.
        % I used median here instead of average because it is a "resistant
        % statistic".
        NOISE_FLOOR = median(mag);
        SNR_THRESHOLD = 15 % dB
        PULSE_THRESHOLD = NOISE_FLOOR*10^(SNR_THRESHOLD/10)

        fprintf('%s - Generating PDWs\n', datestr(now))

        for bin = 1:M
            fc_chan = fc + binFreqs(bin); % frequency in hz for this bin

            pulseActive = false; % keeps track of whether pulse is active
            saturated = false; % keeps track of whether pulse was ever saturated

            for jj = 1:size(iq,1)
                % Look for a leading edge
                if ~pulseActive
                    if mag(jj,bin) >= PULSE_THRESHOLD(bin)
                        pulseActive = true; % a pulse is now active
                        toa = jj; % initialize the time of arrival to current index
                        saturated = false; % initialize whether the pulse was ever saturated
                    end
                else % Look for a trailing edge now that pulse is active
                    if mag(jj,bin) <= PULSE_THRESHOLD(bin) % Declare a trailing edge
                        pulseActive = false; % the pulse is no longer active

                        % compute the UTC time of the time of arrival of the pulse
                        thisToa = ((toa/fs)+sampleStartTime);

                        % compute the amplitude as the median magnitude over the entire pulse
                        thisAmp = median(mag(toa:jj,bin));

                        % compute the SNR for this pulse given the
                        % amplitude and noise floor for this channelizer bin
                        thisSnr = 10*log10(thisAmp/NOISE_FLOOR(bin));

                        % compute the pulse width as the number of samples
                        % this pulse was active for divided by the sampling
                        % rate for this channelizer bin
                        thisPw = (jj-toa)/fs;

                        % compute the median phase difference of this pulse
                        % in order to compute the frequency
                        phaseDiff = diff(phase(toa:jj));
                        phaseDiff(phaseDiff < -180) = phaseDiff(phaseDiff < -180) + 360;
                        phaseDiff(phaseDiff > 180) = phaseDiff(phaseDiff > 180) - 360;
                        medPhaseDiff = median(phaseDiff);

                        % compute the frequency by finding the period given
                        % the median phase difference and then offset it
                        % from the center frequency of this channelizer bin
                        thisFreq = fc_chan+(fs/(360/medPhaseDiff));

                        pdw.toa = [pdw.toa; thisToa];
                        pdw.freq = [pdw.freq; thisFreq];
                        pdw.pw = [pdw.pw; thisPw];
                        pdw.snr = [pdw.snr; thisSnr];
                        pdw.sat = [pdw.sat; saturated];
                    else % Otherwise we're still measuring a pulse
                        if abs(real(iq(jj,bin))) >= 0.9999 || abs(imag(iq(jj,bin))) >= 0.9999
                            saturated = true;
                        end
                    end
                end
            end
        end
    end
end

pdw.sat = pdw.sat == 1;
pdw.d = datetime(1970,1,1,0,0,pdw.toa);

save('pdw.mat','pdw','-v7.3')