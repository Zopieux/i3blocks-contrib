PROGRAM := pulse-volume
CFLAGS := -Wall -Wextra -Wno-unused-parameter -O2

$(PROGRAM): $(PROGRAM).c $(PROGRAM).h
	$(CC) $(CFLAGS) $< -o $@ -lpulse

clean:
	$(RM) $(PROGRAM)

.PHONY: clean
