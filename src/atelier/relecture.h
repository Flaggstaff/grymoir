// La relecture d'un texte transcrit (grammaire, § 15.8) : dans une zone de saisie, les nombres et les mots douteux
// surlignés ; corriger un mot douteux retire son doute. Les marques sont des caractères invisibles (transcription.h).
#pragma once

class QPlainTextEdit;

// Installe le surligneur sur une zone de texte ; sans effet sur un texte qui ne vient pas d'une transcription.
void installer_relecture(QPlainTextEdit *zone);
