// RUN: %ucc -g -S -o- %s | grep -F .loc | cut -d' ' -f3 | grep '1[0-9]' | %stdoutcheck %s
// STDOUT: 12
// STDOUT-NEXT: 11
// STDOUT-NEXT: 10

int f(int);
int g(int);
int h(void);

main()
{
	return
		f(
			g(
				h()));
}
