TARGET      = bouncer
C_SRC       = bouncer_epoll.c common.c
C_DEPS      = $(patsubst %.c,%.d,$(C_SRC))
BOUNCER_OBJ = $(patsubst %.c,%.o,$(C_SRC))

all: $(TARGET)

ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif

bouncer: $(BOUNCER_OBJ)
	$(CC) $^ $(LDFLAGS) -o $@

%o: %c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@"

clean: objclean depclean
	-rm -f $(TARGET)

objclean:
	-rm -f $(BOUNCER_OBJ)

depclean:
	-rm -f $(C_DEPS)

.PHONY: clean
