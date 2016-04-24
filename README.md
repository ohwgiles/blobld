# blobld

Create a 64bit ELF object from a file, with two symbols:

	prefix_start: location of blob
	  prefix_end: end of blob

Useful for embedding resources in an executable

Usage:

	blobld [-s symbol_prefix] [-o output_file] input_file

This essentially replicates

	(GNU) ld --format=binary input_file -o output_file

but more simply and less flexibly.

To access from C:

	extern const char prefix_start[];
	extern const char prefix_end[];
	
	int main() {
		// returns size of blob
		return prefix_end - prefix_start;
	}

Code is public domain
