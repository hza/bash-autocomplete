PREFIX ?= /usr/local
CFLAGS ?= -O2 -Wall -Wextra
PROFILE ?= $(HOME)/.bash_profile
INIT = eval "$$(autocomplete bash)"

autocomplete: autocomplete.c yaml.c yaml.h  ## Build the autocomplete binary
	$(CC) $(CFLAGS) -o $@ autocomplete.c yaml.c

install: autocomplete  ## Install binary and specs, set up ~/.bash_profile
	install -d $(PREFIX)/bin $(PREFIX)/share/autocomplete
	install -m 755 autocomplete $(PREFIX)/bin/autocomplete
	install -m 644 specs/*.yaml $(PREFIX)/share/autocomplete/
	@# skip under sudo: a root-owned ~/.cache would block later rebuilds
	@[ "$$(id -u)" = 0 ] || $(PREFIX)/bin/autocomplete cache
	@if grep -qF 'autocomplete bash' $(PROFILE) 2>/dev/null; then \
		echo "$(PROFILE): already set up"; \
	else \
		if [ -s $(PROFILE) ] && [ -n "$$(tail -c 1 $(PROFILE))" ]; then echo >> $(PROFILE); fi; \
		echo '$(INIT)' >> $(PROFILE); \
		echo '$(PROFILE): added $(INIT)'; \
	fi
	@bin=autocomplete; \
	[ "$$(command -v autocomplete)" = "$(PREFIX)/bin/autocomplete" ] || bin=$(PREFIX)/bin/autocomplete; \
	cmd="eval \"\$$($$bin bash)\""; \
	echo; \
	echo "autocomplete is installed. New terminals will load it automatically."; \
	note=; \
	if command -v pbcopy >/dev/null 2>&1 && printf '%s' "$$cmd" | pbcopy 2>/dev/null; then \
		note="This command is already in your clipboard, so just paste it and press Enter."; \
	fi; \
	echo "To enable it in this terminal right away, run:"; \
	echo; \
	echo "    $$cmd"; \
	echo; \
	[ -z "$$note" ] || { echo "$$note"; echo; }

# Removes only what install added: the binary, our bundled specs, and the
# exact init line (~/.config/autocomplete is left alone).
uninstall:  ## Remove everything install added
	rm -f $(addprefix $${XDG_CACHE_HOME:-$(HOME)/.cache}/autocomplete/,$(patsubst specs/%.yaml,%.cache,$(wildcard specs/*.yaml)))
	rm -f $(PREFIX)/bin/autocomplete
	rm -f $(addprefix $(PREFIX)/share/autocomplete/,$(notdir $(wildcard specs/*.yaml)))
	rmdir $(PREFIX)/share/autocomplete 2>/dev/null || true
	@if grep -qxF '$(INIT)' $(PROFILE) 2>/dev/null; then \
		sed -i '' '/^eval "\$$(autocomplete bash)"$$/d' $(PROFILE); \
		echo '$(PROFILE): removed $(INIT)'; \
	fi

# Precompile all visible specs into ~/.cache/autocomplete/<name>.cache
cache: autocomplete  ## Rebuild the spec cache
	./autocomplete cache

clean:  ## Delete the built binary
	rm -f autocomplete

.PHONY: install uninstall cache clean
