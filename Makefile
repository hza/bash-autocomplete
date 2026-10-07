PREFIX ?= /usr/local
CFLAGS ?= -O2 -Wall -Wextra
PROFILE ?= $(HOME)/.bash_profile
INIT = eval "$$(autocomplete bash)"

autocomplete: autocomplete.c json.c json.h
	$(CC) $(CFLAGS) -o $@ autocomplete.c json.c

install: autocomplete
	install -d $(PREFIX)/bin $(PREFIX)/share/autocomplete
	install -m 755 autocomplete $(PREFIX)/bin/autocomplete
	install -m 644 specs/*.json $(PREFIX)/share/autocomplete/
	@if grep -qF 'autocomplete bash' $(PROFILE) 2>/dev/null; then \
		echo "$(PROFILE): already set up"; \
	else \
		if [ -s $(PROFILE) ] && [ -n "$$(tail -c 1 $(PROFILE))" ]; then echo >> $(PROFILE); fi; \
		echo '$(INIT)' >> $(PROFILE); \
		echo '$(PROFILE): added $(INIT)'; \
	fi

# Removes only what install added: the binary, our bundled specs, and the
# exact init line (~/.config/autocomplete is left alone).
uninstall:
	rm -f $(PREFIX)/bin/autocomplete
	rm -f $(addprefix $(PREFIX)/share/autocomplete/,$(notdir $(wildcard specs/*.json)))
	rmdir $(PREFIX)/share/autocomplete 2>/dev/null || true
	@if grep -qxF '$(INIT)' $(PROFILE) 2>/dev/null; then \
		sed -i '' '/^eval "\$$(autocomplete bash)"$$/d' $(PROFILE); \
		echo '$(PROFILE): removed $(INIT)'; \
	fi

clean:
	rm -f autocomplete

.PHONY: install uninstall clean
