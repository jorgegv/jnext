

const PathUtil = {
    dirpart_for_filepath : function(filepath) {
        filepath = filepath.replaceAll('\\', '/');
        const parts = filepath.split('/')
        if (parts.length > 1) parts.pop();
        return parts.join('/');
    },
    filepart_for_filepath : function(filepath) {
        filepath = filepath.replaceAll('\\', '/');
        const parts = filepath.split('/')
        if (parts.length > 1) return parts.pop();
        return filepath;
    },
    extension_for_filepath : function(filepath) {
        const filepart = this.filepart_for_filepath(filepath);
        const tokens = filepart.split('.');
        if (tokens.length == 0) return '';
        return tokens.pop();
    },
    basename_for_filepath : function(filepath) {
        const filepart = this.filepart_for_filepath(filepath);
        const tokens = filepart.split('.');
        if (tokens.length == 0) return '';
        return tokens.shift();
    },
};

module.exports = { PathUtil };