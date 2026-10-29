ccflags-y += -I$(src)/../include
obj-m += lab_gpio.o
all:
	$(MAKE) -C $(KERNEL_SRC) M=$(CURDIR) modules
modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(CURDIR) modules_install
clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(CURDIR) clean
