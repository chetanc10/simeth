# simeth
SIMulated ETHernet - Linux Device Driver

For creating shared memory backend file for simulating hw-nic memory, create the memory using the following as reference:
sudo dd if=/dev/zero of=/dev/shm/simeth_mem bs=1M count=512

For instantiating VM using qemu with ivshmem (as is required for simeth), refer to the example invocation of qemu below:
sudo qemu-system-x86_64 --enable-kvm -cpu host -object memory-backend-file,size=512M,share,mem-path=/dev/shm/simeth_mem,id=sm1 -device ivshmem,shm=sm1,size=512M -hda ~/ChetaN/junk/cubuntu0.img -m 1514 -net user,hostfwd=tcp::10020-:22 -net nic -nographic -serial mon:stdio

