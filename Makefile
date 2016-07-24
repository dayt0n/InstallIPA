all: build

build:
	gcc main.c -o InstallIPA -lzip -lplist -lcurl -I.

install:
	cp InstallIPA /usr/local/bin/InstallIPA

uninstall:
	rm /usr/local/bin/InstallIPA

clean:
	rm InstallIPA
