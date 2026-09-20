source:
	source ~/esp/esp-idf/export.sh

setup:
	idf.py build

flash:
	idf.py flash
