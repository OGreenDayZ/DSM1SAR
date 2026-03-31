# Règles de nommages

## Visibilité publique / user 
- Fichiers : dsm_nom_de_fichier.h (ici on a juste : dsm.h, donc pas applicable)

## Visibilité interne
- Fichiers : nom.h / nom.c ; (libre)

## Nommages généraux
- Fonctions : dsm_nom_de_fonction
- Type/Enum : dsm_nom_de_type
- Variables globales : dsm_nom_de_variable_globale
- Constantes : DSM_NOM_DE_CONSTANTE
- Macros : DSM_NOM_DE_MACRO
- Header Guards : DSM_NOM_DE_FICHIER_H

Sinon static sur les fonctions uniques au fichier

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