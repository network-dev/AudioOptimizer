#include <unordered_set>
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <cmath>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"
#include "gist/Gist.h"
   
static constexpr float MINIMUM_DB = -50.f;
static constexpr float LIMIT_DB = -6.f;
static constexpr float CONTRAST_DB = 15.f;
static constexpr float EPSILON = 1e-9f;
static constexpr float FREQ_MARGIN = 1.15f;
static constexpr int FRAME_SIZE = 1024;

static const std::unordered_set<std::string> audioExtensions = {
    ".mp3", ".wav", ".ogg", ".aif", ".aiff", 
    ".flac", ".m4a", ".aac", ".wma", ".opus"
};

static const std::vector<int> sampleRates = {
    8000, 11025, 16000, 24000, 32000, 44100
};

std::vector<std::string> warnings;
std::vector<std::string> errors;
 
// Magnitud lineal a decibelios (dB)
float MagToDb(float mag) {
    return 20.0f * std::log10(mag + EPSILON);
}

struct AudioExport {
    ma_uint32 sampleRate;
    bool mono;
    float gain;
    ma_uint64 start, end;
};

bool ExportAudio(const char* in, const char* out, const AudioExport& aExport) {
    // Volvemos a cargar el archivo
    ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder;
    if (ma_decoder_init_file(in, &decoderConfig, &decoder) != MA_SUCCESS) 
        return false;

    bool result = false;
    ma_encoder encoder;
    ma_data_converter converter;

    // Nos vamos en el archivo a la posicion inicial elegida
    ma_decoder_seek_to_pcm_frame(&decoder, aExport.start);

    // Nos preparamos para realizar la exportacion
    ma_uint32 targetChannels = aExport.mono ? 1 : decoder.outputChannels;
    ma_encoder_config encoderConfig = ma_encoder_config_init(
        ma_encoding_format_wav, ma_format_f32, targetChannels, aExport.sampleRate
    );
    if (ma_encoder_init_file(out, &encoderConfig, &encoder) != MA_SUCCESS) {
        ma_decoder_uninit(&decoder);
        return false;
    }
    
    // Nos preparamos para realizar la conversión
    ma_data_converter_config convConfig = ma_data_converter_config_init(
        ma_format_f32, ma_format_f32,
        decoder.outputChannels, targetChannels,
        decoder.outputSampleRate, aExport.sampleRate
    );
    if (ma_data_converter_init(&convConfig, NULL, &converter) != MA_SUCCESS) {
        ma_decoder_uninit(&decoder);
        ma_encoder_uninit(&encoder);
        return false;
    }
        
    const ma_uint64 bufferSize = 4096;
    std::vector<float> bufferIn(bufferSize * decoder.outputChannels);
  
    ma_uint64 maxFramesOut;
    ma_data_converter_get_expected_output_frame_count(&converter, bufferSize, &maxFramesOut);
    std::vector<float> bufferOut(maxFramesOut * 2 * targetChannels);

    ma_uint64 totalFramesToRead = aExport.end - aExport.start;
    ma_uint64 frameCount = 0;

    result = true;
    
    while (frameCount < totalFramesToRead) {
        ma_uint64 framesToRead = std::min(bufferSize, totalFramesToRead - frameCount);
        ma_uint64 framesRead;

        // Abrimos el archivo original
        if (ma_decoder_read_pcm_frames(&decoder, bufferIn.data(), framesToRead, &framesRead) != MA_SUCCESS || framesRead == 0) 
            break;

        ma_uint64 framesIn = framesRead;
        ma_uint64 framesOut = bufferSize * 2;
        
        // Convertimos a nuestro nuevo formato
        ma_data_converter_process_pcm_frames(&converter, bufferIn.data(), &framesIn, bufferOut.data(), &framesOut);

        // Aplicamos la ganancia a los frames
        for (ma_uint64 i = 0; i < framesOut * targetChannels; ++i) {
            bufferOut[i] *= aExport.gain;
        }

        // Escribimos el archivo de salida
        ma_encoder_write_pcm_frames(&encoder, bufferOut.data(), framesOut, NULL);
        
        frameCount += framesRead;
    }

    // Recuperar los frames finales que el resampler tiene en memoria
    ma_uint64 extraFramesOut = 0;
    do {
        ma_uint64 framesIn = 0;
        extraFramesOut = bufferSize;
        ma_data_converter_process_pcm_frames(&converter, NULL, &framesIn, bufferOut.data(), &extraFramesOut);
        if (extraFramesOut > 0) {
            ma_encoder_write_pcm_frames(&encoder, bufferOut.data(), extraFramesOut, NULL);
        }
    } while (extraFramesOut > 0);

    ma_data_converter_uninit(&converter, NULL);
    ma_decoder_uninit(&decoder);
    ma_encoder_uninit(&encoder);
    
    return result;
}

static int ProcessFile(const char* input, const char* base, bool write) {
    std::filesystem::path inputPath(input);
    std::filesystem::path outFolder = "out";

    std::filesystem::path relativePath = std::filesystem::proximate(inputPath, base);

    std::filesystem::path tempWavPath = outFolder / relativePath;
    tempWavPath.replace_extension(".wav");

    std::filesystem::path finalOggPath = outFolder / relativePath;
    finalOggPath.replace_extension(".ogg");

    // Crear la carpeta si no existe
    if (write) {
        std::filesystem::create_directories(finalOggPath.parent_path());
    }

    std::string tempWav = tempWavPath.string();
    std::string finalOgg = finalOggPath.string();
    
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder;
    if(ma_decoder_init_file(input, &config, &decoder) != MA_SUCCESS) 
        return -1;
       
    float sampleRate = static_cast<float>(decoder.outputSampleRate);
    size_t channels = static_cast<size_t>(decoder.outputChannels);
    
    std::vector<float> buffer(FRAME_SIZE * channels);
    std::vector<float> gistFrame(FRAME_SIZE);

    ma_uint64 firstFrame = 0;
    ma_uint64 lastFrame = 0;
    ma_uint64 currentFrame = 0;
    bool foundStart = false;
    bool mono = true; 
    float maxDb = 0;
    float peakSample = 0.0f;
    std::vector<float> frameFreqs; 
    ma_uint64 frameNum;
 
    // Obtenemos el pico máximo
    // No lo hacemos en el mismo bucle para evitar tener un pico equivocado
    while (ma_decoder_read_pcm_frames(&decoder, buffer.data(), FRAME_SIZE, &frameNum) == MA_SUCCESS && frameNum > 0) {
        for (ma_uint64 i = 0; i < frameNum * channels; ++i) {
            float absSample = std::abs(buffer[i]);
            if (absSample > peakSample) peakSample = absSample;
        }
    }
    maxDb = MagToDb(peakSample);
    
    ma_decoder_seek_to_pcm_frame(&decoder, 0);

    Gist<float> gist(FRAME_SIZE, static_cast<int>(sampleRate));
    while(ma_decoder_read_pcm_frames(&decoder, buffer.data(), FRAME_SIZE, &frameNum) == MA_SUCCESS && frameNum > 0) {
        float peak = 0.0f;
        for(size_t i = 0; i < static_cast<size_t>(frameNum); ++i) {
            float sample = 0;
            if(channels >= 2) {
                // Comparamos el canal izquierdo con el derecho
                if (std::abs(buffer[i * channels] - buffer[i * channels + 1]) > EPSILON) {
                    mono = false; // No se puede juntar a mono
                }
                // Para obtener datos de la señal, normalizamos stereo a mono
                sample = (buffer[i * channels] + buffer[i * channels + 1]) / 2.0f;
            }else{
                sample = buffer[i];
            } 
       
            gistFrame[i] = sample;

            // Comparamos con el pico para determinar el más alto
            float absSample = std::abs(sample);
            if (absSample > peak) peak = absSample;
        }

        float peakDb = MagToDb(peak + EPSILON);
        if(peakDb > MINIMUM_DB) {
            if(!foundStart) {
                firstFrame = currentFrame;
                foundStart = true;
            }
            // Siempre actualizamos el final mientras haya sonido
            lastFrame = currentFrame + frameNum;
        }
        if(frameNum < FRAME_SIZE) std::fill(gistFrame.begin() + frameNum, gistFrame.end(), 0.0f);

        gist.processAudioFrame(gistFrame);
        auto mags = gist.getMagnitudeSpectrum();

        float bestFreq = 0;
        for (int i = static_cast<int>(mags.size()) - 1; i >= 0; --i) {
            float freqDb = MagToDb(mags[static_cast<size_t>(i)] + EPSILON);
             
            // Buscamos la primera frecuencia válida
            if (freqDb > MINIMUM_DB && (maxDb - freqDb < CONTRAST_DB)) {
                bestFreq = static_cast<float>(i) * (sampleRate / static_cast<float>(FRAME_SIZE));
                break;
            }
        }
        if (bestFreq > 0) frameFreqs.push_back(bestFreq);
        
        currentFrame += frameNum;
    }
    ma_decoder_uninit(&decoder);
  
    if(mono)
        warnings.emplace_back(std::string(input) + ": Convertido a mono (canales idénticos)");
    
    if (!foundStart) {
        errors.emplace_back(std::string(input) + ": Por debajo del umbral (no procesado)");
        return -1;
    }

    if(firstFrame > 0) {
        float a = static_cast<float>(firstFrame) / sampleRate;
        float b = static_cast<float>(lastFrame)  / sampleRate;
        float c = static_cast<float>(currentFrame) / sampleRate;
        warnings.emplace_back(std::string(input) + ": Recortado: " + std::to_string(a) + "s del inicio " + std::to_string(c-b) + "s del final");
    }

    // Comprobar si estamos saturando o muy bajo y si hay que ajustar el volumen
    float gain = 1.f;
    if(maxDb >= LIMIT_DB) {
        gain = std::pow(10.0f, (LIMIT_DB - maxDb) / 20.0f);
        warnings.emplace_back(std::string(input) + ": Volumen saturado ajustado " + std::to_string(maxDb) + " dB" + " > " + std::to_string(LIMIT_DB) + " dB");
        maxDb = LIMIT_DB;
    }
    else if(maxDb < MINIMUM_DB) {
        gain = std::pow(10.0f, (MINIMUM_DB - maxDb) / 20.0f);
        warnings.emplace_back(std::string(input) + ": Volumen bajo ajustado " + std::to_string(maxDb) + " dB" + " > " + std::to_string(MINIMUM_DB) + " dB");
        maxDb = MINIMUM_DB;
    }
  
    // Percentil 95 para asegurar que la frecuencia elegida es representativa
    float maxFreq = 0;
    if(!frameFreqs.empty()) {
        std::sort(frameFreqs.begin(), frameFreqs.end());
        size_t index = static_cast<size_t>(frameFreqs.size() * 0.95f);
        maxFreq = frameFreqs[index];
    }

    // Ahora calculamos el sample rate nuevo
    int exportSampleRate = static_cast<int>(maxFreq * 2.0f * FREQ_MARGIN);
  
    auto it = std::lower_bound(sampleRates.begin(), sampleRates.end(), exportSampleRate);
    if (it == sampleRates.end()) {
        // Lo dejamos en 44100 porque no se ha encontrado uno válido
        exportSampleRate = sampleRates.back();
    } else {
        exportSampleRate = *it;
    }

    if(maxFreq < 10 || sampleRate < 10) {
        errors.emplace_back(std::string(input) + ": Archivo corrupto (" + std::to_string(maxFreq) + "," + std::to_string(sampleRate) + ")");
        return -1;
    }
 
    std::cout << input << ": " << sampleRate <<  " > " << exportSampleRate << " | " << maxDb << " dB" << " | " << (mono ? "Mono" : "Estéreo") << std::endl;
 
    if(!write) return 0;

    const AudioExport aExport {
        .sampleRate = static_cast<ma_uint32>(exportSampleRate), 
        .mono = mono, 
        .gain = gain, 
        .start = firstFrame, 
        .end = lastFrame
    };

    // Exportar a WAV temporal
    if(ExportAudio(input, tempWav.c_str(), aExport)) {
        // Convertir a OGG usando ffmpeg
        std::string command = 
            "ffmpeg -y -i \"" + tempWav + "\""
            + " " + (mono ? "-ac 1 " : "") 
            + "-c:a libvorbis -q:a 5 " 
            + "\"" + finalOgg + "\" 2>/dev/null";
        
        // Ejecutamos el comando y eliminamos el archivo temporal
        if (system(command.c_str()) == 0) {
            remove(tempWav.c_str());
            return 0;
        }

        errors.emplace_back(std::string(input) + ": Error al usar FFmpeg");
    }
    return 0;
}

std::vector<std::string> GetPaths(const std::string& folder) {
    std::vector<std::string> result;
     
    if(!std::filesystem::exists(folder) || !std::filesystem::is_directory(folder)) 
        return result;

    for(auto const& dir_entry : std::filesystem::recursive_directory_iterator(folder)) {
        if(!dir_entry.is_regular_file()) 
            continue;

        // Obtenemos la extension de archivo y lo pasamos a minusculas
        std::string ext = dir_entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        // Comprobamos si es valido y lo añadimos
        if(audioExtensions.find(ext) != audioExtensions.end()) {
            result.push_back(dir_entry.path().string());
        }
    }
    
    return result;
}

int main(int argc, char* argv[]) {
    bool write = false; 
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-write") == 0) {
            write = true;
            break;
        }
    }

    // Procesamos los archivos de la carpeta
    const char* folder = "in";
    for(const std::string& path : GetPaths(folder)) {
        ProcessFile(path.c_str(), folder, write);
    }

    // Mostramos las acciones tomadas
    std::cout << std::endl;
    std::cout << "--------- CORRECCIONES ---------" << std::endl;
    if(warnings.size() == 0) std::cout << "Sin correcciones" << std::endl;
    for(const std::string& warn : warnings) {
        std::cout << warn << std::endl;
    } 

    // Mostramos los errores encontrados
    std::cout << std::endl;
    std::cout << "--------- ERRORES ---------" << std::endl;
    if(errors.size() == 0) std::cout << "Sin errores" << std::endl;
    for(const std::string& error : errors) {
        std::cout << error << std::endl;
    }
 
    return 0;
}