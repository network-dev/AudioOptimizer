# Optimizador de Audio

Este programa optimiza archivos de audio.

## Características

* Reducción de la frecuencia de muestreo (downsampling) al nivel óptimo.
* Conversión de estéreo a mono cuando sea necesario.
* Recorte automático de silencios al inicio y al final de los clips.
* Conversión de todos los archivos a formato .ogg para una mejor compresión.
* Normalización para evitar saturación, recortes (clipping) y volumen bajo.

## Requisitos

* FFmpeg: Debe estar instalado y disponible en la variable de entorno PATH del sistema.
* Estructura de directorios: Crea una carpeta llamada 'in' en la raíz del proyecto para colocar tus archivos de origen.
* Herramientas de compilación (clang++, g++), Homebrew (MacOS)

## Formatos Compatibles

El programa busca en la carpeta 'in' las siguientes extensiones:
.mp3, .wav, .ogg, .aif, .aiff, .flac, .m4a, .aac, .wma, .opus

## Sistemas Operativos Compatibles

* macOS 13 (Arm64) y Linux (Arch) de forma nativa.
* Puede funcionar en otros sistemas siempre que se proporcionen las librerías fftw3.

## Uso

1. Coloca tus archivos de audio en la carpeta 'in'.
2. Previsualizar acciones (simulación):
   make run
3. Procesar y exportar archivos:
   make write