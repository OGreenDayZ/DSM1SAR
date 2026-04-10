Pas d'inclusion de type : "../../dsm.h" ou "../pager/pager.h"

Ecrire plutôt : 
#include "dsm.h"
#include "pager/pager.h"

Pour corriger le rouge, mettre dans .vscode, un ```c_cpp_properties.json``` avec :
```
{
    "configurations": [
        {
            "name": "DSM1SAR-Config",
            "includePath": [
                "${workspaceFolder}/include",
                "${workspaceFolder}/src"
            ]
        }
    ],
    "version": 4
}
```

Le Makefile gèrera les inclusions avec le flag -I dans CFLAGS


Pour les headers, pour automatiser, avec snippets globaux, inclure ça dans le json : 
```
	"Include Guard DSM": {
		"scope": "c,cpp",
		"prefix": "hdsm",
		"body": [
			"#ifndef DSM_${TM_FILENAME_BASE/(.*)/${1:/upcase}/}_H",
			"#define DSM_${TM_FILENAME_BASE/(.*)/${1:/upcase}/}_H",
			"",
			"$0",
			"",
			"#endif // DSM_${TM_FILENAME_BASE/(.*)/${1:/upcase}/}_H"
		],
		"description": "Génère des gardes d'inclusion DSM basés sur le nom du fichier"
	}
```
