#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <strings.h>
#include <inttypes.h>
#include <string.h>

#include "lmc.h"

/* Littleman computer args
 * 000 HLT         halt the program
 * 1xx ADD [loc]   add  value at location to register value
 * 2xx SUB [loc]   subtract value at location from register value
 * 3xx STA [loc]   store register value at memory location
 * 5xx LDA [loc]   load value from memory to register
 * 6xx BRA [loc]   set pc to value
 * 7xx BRZ [loc]   set pc to value if register is 0
 * 8xx BRP [loc]   set pc to value if positive
 * 901 INP         read input as byte to register
 * 902 OUT         print register value as ascii
 */

#define LMC_MEM_MAX 999

struct lmc_state {
	lmc_int reg;
	lmc_int pc;

	lmc_int memlen;
	lmc_int memory[1];
};

static inline lmc_int command_from_code(lmc_int c)
{
	return c / 100;
}

static inline lmc_int data_from_code(lmc_int c)
{
	return c - (command_from_code(c) * 100);
}

struct lmc_state* lmc_alloc(lmc_int memory_size, void* (*allocate)(size_t))
{
	assert(memory_size <= 100); // More memory not supported by 'bytecode' format

	if (allocate == NULL) allocate = malloc;

	// Add one to the memory size for a 
	// always-HLT instruction to prevent
	// execution outside of the array
	memory_size += 1;

	struct lmc_state* s = allocate(sizeof(struct lmc_state) + sizeof(lmc_int) * (memory_size - 1));
	if (s == NULL)
		return NULL;

	s->reg = 0;
	s->pc = 0;
	s->memlen = memory_size - 1;


	for (unsigned int i = 0; i < memory_size; ++i) {
		s->memory[i] = 0;
	}

	return s;
}

void lmc_destroy(struct lmc_state* s, void (*release)(void*))
{
	assert(s != NULL);

	if (release == NULL) release = free;

	if (s != NULL) release(s);
}

int lmc_execute_instruction(struct lmc_state* s, FILE* input, FILE* output)
{
	// Don't walk past the end of memory
	if (s->pc >= s->memlen) return 0;

	lmc_int code = s->memory[s->pc++];
	lmc_int data = data_from_code(code);

	if (data >= s->memlen)
		return 0;

	switch (command_from_code(code)) {
		case 0: // HLT
			return 0;
		case 1: // ADD
			s->reg += s->memory[data];
			return 1;
		case 2: // SUB
			s->reg -= s->memory[data];
			return 1;
		case 3: // STA
			s->memory[data] = s->reg;
			return 1;
		case 5: // LDA
			s->reg = s->memory[data];
			return 1;
		case 6: // BRA
			s->pc = data;
			return 1;
		case 7: // BRZ
			if (s->reg == 0)
				s->pc = data;
			return 1;
		case 8: // BRP
			if (s->reg > 0)
				s->pc = data;
			return 1;
		case 9: // INP/OUT
			if (data != 1 && data != 2)
				return 0;
			if (data == 1) {
				int inp = getc(input);
				if (inp == EOF)
					return 0;
				if(inp < 0 || inp > LMC_MEM_MAX)
					return 0;
				// TODO: What to do on EOF,
				//       and what to do on
				//       out-of memrange inp?
				s->reg = (lmc_int)inp;
			} else if (data == 2) {
				putc(s->reg, output);
			}
			return 1;
		default:
			assert(0);
			return 0;
	}
}

static const char* commandtexts[] = {
	"hlt",
	"add",
	"sub",
	"sta",
	"",
	"lda",
	"bra",
	"brz",
	"brp",
	"inp",
	"out"
};

void lmc_debugprint(struct lmc_state* s, FILE* stream)
{
	for(lmc_int i = 0; i < s->memlen; ++i) {
		if (i != 0)
			fputs(", ", stream);
		if (s->pc == i)
			fprintf(stream, "[%i]:", i);
		else
			fprintf(stream, "%i:", i);

		if (s->memory[i] < 100 || command_from_code(s->memory[i]) == 4) {
			fprintf(stream, "{%i}", s->memory[i]);
		} else {
			lmc_int cmd = command_from_code(s->memory[i]);
			const char* arg = commandtexts[cmd];
			if (cmd == 9 && s->memory[i] == 902) {
				arg = commandtexts[cmd+1];
			}
			fprintf(stream, "{%i (%s %i)}", s->memory[i], arg, 
			                data_from_code(s->memory[i]));
		}
	}
	fputc('\n', stream);
	fputs("Press to continue\n", stdout);
	getchar();
	fputc('\n', stream);
}

#define LEX_BUF_SIZE (1024 * 2)

#define NOWORD SIZE_MAX

struct lex_state {
	size_t numread;
	int mode;
	size_t i;
	size_t wordstart;
	size_t row;
	char input[LEX_BUF_SIZE];
};

struct string_with_length {
	size_t len;
	const char* data;
};

static struct string_with_length lex(FILE* stream, struct lex_state* s)
{
	while(1) {
		if (s->i == s->numread || s->mode == 0) {
			// Need to read
			if (s->numread < LEX_BUF_SIZE && s->mode != 0) {
				// If EOF or error, return NULL value to
				// signal 'done'
				struct string_with_length ret = { 0, NULL };
				return ret;
			}
			if (s->mode == 4) {
				// Read more, while preserving everything
				// after wordstart
				if (s->wordstart != 0 && s->i - s->wordstart > 0) {
					memcpy(s->input, &(s->input[s->wordstart]), s->numread - s->wordstart);
				}
				s->numread = fread(&(s->input[s->numread - s->wordstart]),
				                   sizeof(char), 
				                   LEX_BUF_SIZE - (size_t)(s->numread - s->wordstart), 
				                   stream);
				s->i -= s->wordstart;
				s->wordstart = 0;
			} else {
				s->numread = fread(s->input, sizeof(char), LEX_BUF_SIZE, stream);
				s->i = 0;
				s->mode = 1;
			}

			if (s->numread == 0) {
				// EOF or read failed
				struct string_with_length ret = { 0, NULL };
				return ret;
			}
		}
		switch (s->mode) {
			case 1: // Started, or after newline
				if (s->input[s->i] == '\n') {
					s->row++;
					s->i++;
				} else if (s->input[s->i] == '#') {
					// Comment
					s->mode = 2;
				} else if(isspace(s->input[s->i])) {
					s->mode = 3;
				} else {
					s->mode = 3;
				}
				break;
			case 2: // Find next newline
				if (s->input[s->i++] == '\n') {
					s->row++;
					s->mode = 1;
				}
				break;
			case 3: // Find next word
				if (s->input[s->i] == '\n') {
					s->mode = 1;
				} else if (!isspace(s->input[s->i])) {
					if (s->input[s->i] == '"') {
						s->mode = 5;
					} else if (s->input[s->i] == '\'') {
						s->mode = 6;
					} else {
						s->mode = 4;
					}
				} else {
					s->i++;
				}
				break;
			case 4: // Read word
			case 5: // Read string
			case 6: // Read char literal
				if (s->wordstart == NOWORD)
					s->wordstart = s->i;

				int isdone = 0;
				if (s->mode == 4) {
					isdone = isspace(s->input[s->i]);
				} else if (s->mode == 5) {
					isdone = s->wordstart != s->i && (s->input[s->i] == '"');
				} else if (s->mode == 6) {
					isdone = s->wordstart != s->i && (s->input[s->i] == '\'');
				}
				if (isdone) {
					// include terminator
					if (s->mode == 5 || s->mode == 6)
						s->i++;

					if (s->input[s->i] == '\n')
						s->mode = 1;
					else
						s->mode = 3;

					size_t wordstart = s->wordstart;
					s->wordstart = NOWORD;

					struct string_with_length ret = {
						s->i - wordstart,
						s->input + wordstart
					};
					return ret;
				} else {
					if (s->input[s->i] == '\n')
						s->row++;
					s->i++;
				}
				break;
		}
	}
}

#define UNDEFADDR 100

#define NLABELS 512

struct label {
	lmc_int address;
	int used;
	char text[127];
	lmc_int usage[512-127];
};

#define PARSEERROR(s, l) \
do { \
	fprintf(stderr, "%s:%zu: ", s, l.row); \
} while(0)

#define INCI() \
do { \
	i++; \
	if (i >= s->memlen) { \
		fputs("Input file is larger than the current memory size\n", stderr); \
		return 1; \
	} \
} while(0)

static int parse(struct lmc_state* s, const char* filename, FILE* stream)
{
	struct label labels[NLABELS];
	int maxlabel = 0;

	struct lex_state lexer;
	lexer.numread = 0;
	lexer.mode = 0;
	lexer.i = 0;
	lexer.wordstart = NOWORD;
	lexer.row = 1;

	lmc_int i = 0;
	int mode = 0;
	int result = 0;
	while(1) {
		struct string_with_length word = lex(stream, &lexer);

		if (word.data == NULL) {
			if (mode == 0) {
				// Not currently expecting data
				if (i > 0 && s->memory[i-1] != 0) {
					// If the last instruction wasnt HLT,
					// assume it was
					s->memory[i] = 0;
				}
				break;
			} else {
				PARSEERROR(filename, lexer);
				fputs("Unexpected EOF while reading instruction data\n", stderr);
				result++;
				break;
			}
		}

		switch (mode) {
			case 0: // Command or label
				if (word.len != 3 || word.data[word.len-1] == ':') {
					if (word.data[word.len-1] == ':') {
						int found = 0;
						for(int j = 0; j < maxlabel; ++j) {
							if (strlen(labels[j].text) != word.len-1)
								continue;

							if (strncmp(labels[j].text, word.data, word.len-1) == 0) {
								if (labels[j].address == UNDEFADDR) {
									// Not defined yet.
									// Define it and update all locations it's used
									labels[j].address = i;
									for(int h = 0; h < labels[j].used; ++h) {
										s->memory[labels[j].usage[h]] += labels[j].address;
									}
									labels[j].used = 0;
								} else {
									goto duplicate_label;
								}
								found = 1;
								mode = 	0;
								break;
							}
						}

						if (!found) {
							strncpy(labels[maxlabel].text, word.data, word.len-1);
							labels[maxlabel].text[word.len-1] = '\0';
							labels[maxlabel].address = i;
							labels[maxlabel].used = 0;
							maxlabel++;
						}
						break;
					} else {
						goto unknown_instruction;
					}
				}
				if (strncasecmp(word.data, "DAT", 3) == 0) {
					s->memory[i] = 0;
					mode = 2;
				} else if (strncasecmp(word.data, "LDA", 3) == 0) {
					s->memory[i] = 500;
					mode = 1;
				}
				else if (strncasecmp(word.data, "STA", 3) == 0) {
					s->memory[i] = 300;
					mode = 1;
				}
				else if (strncasecmp(word.data, "BRZ", 3) == 0) {
					s->memory[i] = 700;
					mode = 1;
				}
				else if (strncasecmp(word.data, "BRA", 3) == 0) {
					s->memory[i] = 600;
					mode = 1;
				}
				else if (strncasecmp(word.data, "BRP", 3) == 0) {
					s->memory[i] = 800;
					mode = 1;
				}
				else if (strncasecmp(word.data, "ADD", 3) == 0) {
					s->memory[i] = 100;
					mode = 1;
				}
				else if (strncasecmp(word.data, "SUB", 3) == 0) {
					s->memory[i] = 200;
					mode = 1;
				}
				else if (strncasecmp(word.data, "INP", 3) == 0) {
					s->memory[i] = 901;
					mode = 0;
					INCI();
				}
				else if (strncasecmp(word.data, "OUT", 3) == 0) {
					s->memory[i] = 902;
					mode = 0;
					INCI();
				}
				else if (strncasecmp(word.data, "HLT", 3) == 0) {
					s->memory[i] = 000;
					mode = 0;
					INCI();
				} else {
					goto unknown_instruction;
				}
				break;
unknown_instruction:
				PARSEERROR(filename, lexer);
				fputs("Unknown instruction '", stderr);
				fwrite(word.data, sizeof(char), word.len, stderr);
				fputs("'\n", stderr);
				result++;
				break;
duplicate_label:
				PARSEERROR(filename, lexer);
				fputs("Duplicate label '", stderr);
				fwrite(word.data, sizeof(char), word.len, stderr);
				fputs("'\n", stderr);
				result++;
				break;
			case 1: // Data
			case 2: // Data for DAT command
				if (word.data[0] == '&') {
					// Label
					int found = 0;
					for(int j = 0; j < maxlabel; ++j) {
						if (strlen(labels[j].text) != word.len-1)
							continue;

						if (strncmp(labels[j].text, word.data+1, word.len-1) == 0) {
							if (labels[j].address == UNDEFADDR) {
								// Not defined yet
								labels[j].usage[labels[j].used++] = i;
							} else {
								s->memory[i] += labels[j].address;
							}
							found = 1;
							break;
						}
					}
					if (!found) {
						strncpy(labels[maxlabel].text, word.data + 1, word.len-1);
						labels[maxlabel].text[word.len-1] = '\0';
						labels[maxlabel].address = UNDEFADDR;
						labels[maxlabel].used = 1;
						labels[maxlabel].usage[0] = i;
						maxlabel++;
					}

					mode = 0;
					INCI();
				} else if (word.data[0] == '"') {
					if (mode != 2) {
						PARSEERROR(filename, lexer);
						fputs("String literal not permitted outside of data definition\n", stderr);
						result++;
						mode = 0;
					}
					if (word.len == 2) {
						PARSEERROR(filename, lexer);
						fputs("Empty string literal\n", stderr);
						result++;
						mode = 0;
					} else if (word.data[word.len-1] != '"') {
						PARSEERROR(filename, lexer);
						fputs("Unterminated string literal\n", stderr);
						result++;
						mode = 0;
					} else {
						for (lmc_int j = 1; j < word.len-1; ++j) {
							s->memory[i] = (lmc_int)word.data[j];
							INCI();
						}
						mode = 0;
					}
				} else if (word.data[0] == '\'') {
					if (mode != 2) {
						PARSEERROR(filename, lexer);
						fputs("Character literal not permitted outside of data definition\n", stderr);
						result++;
						mode = 0;
						break;
					}
					if (word.len != 3 || word.data[2] != '\'') {
						PARSEERROR(filename, lexer);
						fputs("Invalid character literal\n", stderr);
						result++;
						mode = 0;
						break;
					}
					s->memory[i] += (lmc_int)word.data[1];
					mode = 0;
					INCI();
				} else {
					for (size_t j = 0; j < word.len; ++j) {
						if (!isdigit(word.data[j])) {
							mode = 0;
							goto invalid_value;
						}
					}
					int value = atoi(word.data);
					if (value < 0 || value > LMC_MEM_MAX) {
						mode = 0;
						goto invalid_value;
					}

					s->memory[i] += value;
					mode = 0;
					INCI();
				}
				break;
invalid_value:
				PARSEERROR(filename, lexer);
				fputs("Invalid value '", stderr);
				fwrite(word.data, sizeof(char), word.len, stderr);
				fputs("'\n", stderr);
				result++;
				break;
		}
	}

	for (int k = 0; k < maxlabel; ++k) {
		if (labels[k].used > 0) {
			fprintf(stderr, "Unknown label %s\n", labels[k].text);
			result++;
		}
	}

	return result;
}

int lmc_load_file(struct lmc_state* s, const char* filename, FILE* stream)
{
	int parse_res = parse(s, filename, stream);
	if (parse_res == 0) {
		// Success
		return 0;
	} else {
		fprintf(stderr, "%s: Parsing encountered %i errors\n", filename, parse_res);
		return -1;
	}
}
