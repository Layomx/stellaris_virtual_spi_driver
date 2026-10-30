set pagination off
target remote localhost:1234
break main
break spi_cs_assert
break spi_transfer
